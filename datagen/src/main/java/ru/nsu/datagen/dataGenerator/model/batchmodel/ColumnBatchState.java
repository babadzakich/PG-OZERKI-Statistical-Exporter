package ru.nsu.datagen.dataGenerator.model.batchmodel;

import java.util.List;

import lombok.Getter;
import ru.nsu.datagen.dataGenerator.generators.ColumnGenerator;
import ru.nsu.datagen.dataGenerator.generators.normal.NormalValueGenerator;
import ru.nsu.datagen.dataGenerator.generators.unique.UniqueKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;

/**
 * Обёртка над {@link ColumnGenerator} и ассоциированными с ним колонками,
 * хранящая текущее состояние генерации ({@link StateData}).
 *
 * <p>Поддерживает откат к предыдущему состоянию ({@link #rollbackToPreviousState()})
 * на случай ошибки сохранения батча, а также точечное пересэмплирование строк
 * с constraint violation через {@link #regenerateRows(int)}.
 */
public class ColumnBatchState {
    private final ColumnGenerator valueGenerator;
    @Getter private final List<ColumnMetadata> columns;
    @Getter private StateData curStateData;
    private StateData prevStateData;

    public ColumnBatchState(NormalValueGenerator valueGenerator, ColumnMetadata columnMetadata) {
        this.valueGenerator = valueGenerator;
        this.columns = List.of(columnMetadata);
        this.curStateData = new StateData(columnMetadata);
        this.prevStateData = null;
    }

    public ColumnBatchState(UniqueKeyGenerator valueGenerator, List<ColumnMetadata> compositePeers) {
        this.valueGenerator = valueGenerator;
        this.columns = compositePeers;
        this.curStateData = new StateData(compositePeers);
        this.prevStateData = null;
    }

    public ColumnBatchState(ColumnGenerator valueGenerator, ColumnMetadata columnMetadata) {
        this.valueGenerator = valueGenerator;
        this.columns = List.of(columnMetadata);
        this.curStateData = new StateData(columnMetadata);
        this.prevStateData = null;
    }

    public ColumnBatchState(ColumnGenerator valueGenerator, List<ColumnMetadata> columns) {
        this.valueGenerator = valueGenerator;
        this.columns = columns;
        this.curStateData = new StateData(columns);
        this.prevStateData = null;
    }

    /**
     * Откатывает текущее состояние генерации к состоянию до последнего вызова {@link #produceBatch}.
     * Не имеет эффекта, если откат уже был выполнен или батч ещё не генерировался.
     */
    public void rollbackToPreviousState() {
        if (this.prevStateData != null) {
            this.curStateData = this.prevStateData;
        }
    }

    /**
     * Генерирует батч с нулевым счётчиком пустых батчей.
     *
     * @param batchSize желаемое число строк
     * @return список колонок (внешний) со значениями строк (внутренний)
     */
    public List<List<Object>> produceBatch(int batchSize) {
        return produceBatch(batchSize, 0);
    }

    /**
     * Сохраняет текущее состояние и генерирует следующий батч данных.
     *
     * @param batchSize       желаемое число строк
     * @param emptyBatchCount число подряд идущих батчей с нулевым результатом сохранения
     * @return список колонок (внешний) со значениями строк (внутренний)
     */
    public List<List<Object>> produceBatch(int batchSize, int emptyBatchCount) {
        this.prevStateData = new StateData(this.curStateData);
        this.curStateData.setEmptyBatchCount(emptyBatchCount);
        return valueGenerator.generateValues(batchSize, curStateData);
    }

    public List<List<Object>> regenerateRows(int count) {
        if (valueGenerator instanceof UniqueKeyGenerator ukg) {
            return ukg.regenerateRows(count);
        }
        return List.of();
    }
}
