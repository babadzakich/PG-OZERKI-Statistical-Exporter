package ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.fk.ForeignKeyGeneratorFactory;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGenerator;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorFactory;
import ru.nsu.datagen.dataGenerator.generators.unique.UniqueKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.ReferencingTreeNode;
import ru.nsu.datagen.dataGenerator.model.batchmodel.StateData;

@Slf4j
public class SimpleUniqueGenerator implements UniqueKeyGenerator {
    private final ColumnMetadata column;
    private final int recordCount;
    private final List<ReferencingTreeNode> referencingTrees;
    private final Map<String, List<Object>> allGeneratedData;

    private List<Object> cachedValues;
    // Параллельный Set для O(1) дедупа при ленивом расширении cachedValues.
    // Нужен потому что ID расходуются быстрее чем вставляются: removed_unjoined_columns
    // двигает offset вперёд для каждого отклонённого батча (из-за Markov/FK нарушений
    // в других колонках), и cachedValues может закончиться раньше recordCount инсертов.
    private Set<Object> cachedValuesSet;

    public SimpleUniqueGenerator(List<ColumnMetadata> uniqColumns, int recordCount,
                                 Map<String, List<ReferencingTreeNode>> referencingTrees,
                                 Map<String, List<Object>> allGeneratedData) {
        this.column = uniqColumns.getFirst();
        this.referencingTrees = referencingTrees == null ? null : referencingTrees.get(this.column.getName());
        this.recordCount = recordCount;
        this.allGeneratedData = allGeneratedData;
    }

    @Override
    public List<Object> generate() {
        return getOrBuildValues();
    }

    @Override
    public void generate(Map<String, List<Object>> columnData) {
        columnData.put(column.getName(), getOrBuildValues());
    }

    @Override
    public void generate(Map<String, List<Object>> columnData, int offset, int batchSize) {
        List<Object> all = getOrBuildValues();
        int end = Math.min(offset + batchSize, all.size());
        columnData.put(column.getName(), offset >= end ? List.of() : new ArrayList<>(all.subList(offset, end)));
    }

    @Override
    public List<List<Object>> generateValues(int batchSize, StateData stateData) {
        List<Object> all = getOrBuildValues();
        int offset = stateData.getGeneratedCount();
        int need = offset + batchSize;
        // Ленивое расширение: removeUnaddedColumns сдвигает generatedCount назад на каждый
        // отклонённый батч, но при этом уже «потраченные» индексы (rejected rows) не возвращаются.
        // Значит, all.size() может кончиться раньше чем createdAmount дойдёт до recordCount.
        if (need > all.size() && cachedValuesSet != null) {
            extendCachedValues(need - all.size() + batchSize);
        }
        int end = Math.min(need, all.size());
        List<Object> batch = offset >= end ? List.of() : new ArrayList<>(all.subList(offset, end));
        stateData.advance(batch.size());
        return List.of(batch);
    }

    private List<Object> getOrBuildValues() {
        if (cachedValues == null) {
            Set<Object> unique = buildUniqueSet();
            cachedValues = new ArrayList<>(unique);
            // Сохраняем Set для дедупа при последующих расширениях.
            // Для FK-колонок расширение не нужно — Set не нужен.
            if (!column.isForeignKey()) {
                cachedValuesSet = new HashSet<>(unique);
            }
        }
        return cachedValues;
    }

    private void extendCachedValues(int needed) {
        ValueGenerator generator = ValueGeneratorFactory.createValueGenerator(column);
        boolean hasHistogram = column.getHistogramm() != null;
        Object leftBorder = hasHistogram ? column.getHistogramm().getFirst() : null;
        Object rightBorder = hasHistogram ? column.getHistogramm().getLast() : null;

        int added = 0;
        int safety = Math.max(1, needed) * 100;
        int attempts = 0;
        while (added < needed && attempts < safety) {
            attempts++;
            Object v = hasHistogram
                    ? generator.generateValue(leftBorder, rightBorder)
                    : generator.generateValue();
            if (cachedValuesSet.add(v)) {
                cachedValues.add(v);
                added++;
            }
        }
        if (added < needed) {
            log.warn("Колонка {}: не удалось расширить пул на {} значений, добавлено {} (диапазон исчерпан?)",
                    column.getName(), needed, added);
        } else {
            log.debug("Колонка {}: пул расширен до {} значений", column.getName(), cachedValues.size());
        }
    }

    // Возвращает Set уникальных значений. Для FK-колонок — берём из ForeignKeyGeneratorFactory
    // и не поддерживаем расширение. Для обычных — LinkedHashSet с MCV+referenced в начале.
    private Set<Object> buildUniqueSet() {
        if (column.isForeignKey()) {
            List<Object> fkValues = ForeignKeyGeneratorFactory.getInstance()
                    .getGenerator(List.of(column), allGeneratedData)
                    .generateValues(column.getRecordCount(), new StateData(column))
                    .getFirst();
            return new LinkedHashSet<>(fkValues);
        }

        // LinkedHashSet: сначала MCV/referenced (гарантированно попадут в итог),
        // потом сгенерированные. Порядок детерминирован — срезы батчей воспроизводимы.
        Set<Object> unique = new LinkedHashSet<>(column.getMcv().keySet());
        if (referencingTrees != null) {
            for (ReferencingTreeNode node : referencingTrees) {
                collectReferencedValues(unique, node);
            }
        }
        if (column.getNullCount() > 0) {
            unique.add(null);
        }

        ValueGenerator generator = ValueGeneratorFactory.createValueGenerator(column);
        boolean hasHistogram = column.getHistogramm() != null;
        Object leftBorder = hasHistogram ? column.getHistogramm().getFirst() : null;
        Object rightBorder = hasHistogram ? column.getHistogramm().getLast() : null;

        // Генерируем recordCount кандидатов в отдельный список и дедуплицируем через Set.
        // Нельзя передавать unique напрямую в generateValues: часть реализаций (LongValueGenerator,
        // SmallintValueGenerator и др.) не проверяют existing-элементы и добавляют дубликаты.
        List<Object> batch = new ArrayList<>(recordCount);
        if (hasHistogram) {
            generator.generateValues(batch, recordCount, leftBorder, rightBorder);
        } else {
            generator.generateValues(batch, recordCount);
        }
        for (Object v : batch) {
            if (unique.size() >= recordCount) break;
            unique.add(v);
        }

        // Добираем если генератор насовал дубликатов
        int safety = Math.max(1, recordCount) * 10;
        int attempts = 0;
        while (unique.size() < recordCount && attempts < safety) {
            attempts++;
            Object v = hasHistogram ? generator.generateValue(leftBorder, rightBorder) : generator.generateValue();
            unique.add(v);
        }
        if (unique.size() < recordCount) {
            log.warn("Колонка {}: запрошено {} уникальных значений, получено {} (диапазон ограничен?)",
                    column.getName(), recordCount, unique.size());
        }

        return unique;
    }

    private void collectReferencedValues(Set<Object> values, ReferencingTreeNode node) {
        values.addAll(node.getToAdd());
        for (ReferencingTreeNode child : node.getChildren()) {
            collectReferencedValues(values, child);
        }
    }
}
