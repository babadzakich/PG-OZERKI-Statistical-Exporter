package ru.nsu.datagen.dataGenerator.generators.normal;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.ThreadLocalRandom;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGenerator;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorFactory;
import ru.nsu.datagen.dataGenerator.generators.unique.UniqueKeyGenerator;
import ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators.SimpleUniqueGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;

@Slf4j
public class StatTypeBasedGenerator implements NormalValueGenerator {
    private final ThreadLocalRandom random = ThreadLocalRandom.current();

    @Override
    public List<Object> generateValues(ColumnMetadata columnMetadata, int batchSize) {
        log.info("Generating batch of {} values for column: {}", batchSize, columnMetadata.getName());
        if (columnMetadata.getNdistinct() == -1) {
            log.debug("Using SimpleUniqueGenerator for column: {}, because ndistinct = -1", columnMetadata.getName());
            UniqueKeyGenerator generator = new SimpleUniqueGenerator(List.of(columnMetadata), columnMetadata.getRecordCount(), null, null);
            return generator.generate();
        }
        Set<Object> objectSet = new HashSet<>();

        long requiredUniqueCount = columnMetadata.getNdistinct() < 0
            ? (long)(Math.abs(columnMetadata.getNdistinct()) * columnMetadata.getRecordCount())
            : (long)columnMetadata.getNdistinct();
        log.debug("Generating {} unique values for column {} where ndistinct = {}", requiredUniqueCount, columnMetadata.getName(), columnMetadata.getNdistinct());
        List<Object> values = new ArrayList<>();
        int recordCount = columnMetadata.getRecordCount();
        log.debug("Total record count for column {}: {}", columnMetadata.getName(), recordCount);
        double nullPercentage = columnMetadata.getNullPercentage();
        log.debug("Null percentage for column {}: {}", columnMetadata.getName(), nullPercentage);

        log.debug("Processing MCVs for column {}", columnMetadata.getName());
        for (Object mvcValue : columnMetadata.getMcv().keySet()) {
            objectSet.add(mvcValue);
            double freq = columnMetadata.getMcv().get(mvcValue);
            long mvcCount = Math.round(freq * recordCount);
            log.trace("Adding MVC value: {} with frequency: {} resulting in count: {}", mvcValue, freq, mvcCount);
            for (long j = 0; j < mvcCount; j++) {
                values.add(mvcValue);
            }
        }

        int nullCount = (int)(recordCount * (nullPercentage / 100.0));
        log.debug("Adding {} null values for column {}", nullCount, columnMetadata.getName());
        for (int i = 0; i < nullCount; i++) {
            values.add(null);
        }

        long mvcCount = objectSet.size();
        long hasNulls = nullPercentage > 0 ? 1 : 0;
        long remainingUniqueCount = requiredUniqueCount - mvcCount - hasNulls;
        long remainingValueCount = recordCount - values.size();
        log.debug("Remaining unique count to generate for column {}: {}", columnMetadata.getName(), remainingUniqueCount);
        log.debug("Remaining value count to fill for column {}: {}", columnMetadata.getName(), remainingValueCount);

        ValueGenerator generator = ValueGeneratorFactory.createValueGenerator(columnMetadata);

        int histogramBuckets = columnMetadata.getHistogramm().size() - 1;

        if (histogramBuckets > 0 && remainingUniqueCount > 0 && remainingValueCount > 0) {
            log.debug("Using histogram-based generation for column {} with {} buckets", columnMetadata.getName(), histogramBuckets);
            for (int i = 0; i < histogramBuckets; i++) {
                Object lowerBound = columnMetadata.getHistogramm().get(i);
                Object upperBound = columnMetadata.getHistogramm().get(i + 1);
                log.trace("Bucket {}: Lower bound = {}, Upper bound = {}", i, lowerBound, upperBound);

                // Распределяем уникальные значения равномерно по бакетам с учётом остатка
                // Например: 2002 значения на 100 бакетов = 20 + (1 если i < 2), т.е. первые 2 бакета по 21, остальные по 20
                long baseUniqueCount = remainingUniqueCount / histogramBuckets;
                long uniqueRemainder = remainingUniqueCount % histogramBuckets;
                long rangeUniqueCount = baseUniqueCount + (i < uniqueRemainder ? 1 : 0);

                // Распределяем общее количество значений равномерно по бакетам с учётом остатка
                long baseValueCount = remainingValueCount / histogramBuckets;
                long valueRemainder = remainingValueCount % histogramBuckets;
                long rangeValueCount = baseValueCount + (i < valueRemainder ? 1 : 0);

                log.trace("Bucket {}: Calculated unique count = {}, value count = {}", i, rangeUniqueCount, rangeValueCount);

                // Генерируем уникальные значения для данного бакета
                List<Object> bucketUniqueValues = new ArrayList<>();
                bucketUniqueValues.add(lowerBound);
                bucketUniqueValues.add(upperBound);

                for (long j = 2; j < rangeUniqueCount; j++) {
                    Object val = generator.generateValue(lowerBound, upperBound);
                    int attempts = 0;
                    while (!objectSet.add(val) && attempts < 10000) {
                        val = generator.generateValue(lowerBound, upperBound);
                        attempts++;
                    }
                    if (attempts < 10000) {
                        bucketUniqueValues.add(val);
                    }
                }
                log.trace("Bucket {}: Generated {} unique values: {}", i, bucketUniqueValues.size(), bucketUniqueValues);

                // Заполняем values случайными значениями из сгенерированных уникальных
                if (!bucketUniqueValues.isEmpty()) {
                    log.trace("Bucket {}: Filling {} values from unique values", i, rangeValueCount);
                    for (int j = i == 0 ? 0 : 1; j < rangeUniqueCount; j++) {
                        values.add(bucketUniqueValues.get(j));
                    }
                    log.trace("Bucket {}: Added {} unique values, now filling remaining {} values", i, rangeUniqueCount, rangeValueCount - rangeUniqueCount);
                    for (long j = rangeUniqueCount; j < rangeValueCount; j++) {
                        values.add(bucketUniqueValues.get(random.nextInt(bucketUniqueValues.size())));
                    }
                    log.trace("Bucket {}: Filled {} values", i, rangeValueCount);
                } else {
                    log.warn("Warning: Could not generate unique values for bucket {}", i);
                }
            }
        }

        // Fallback: если после обработки гистограммы все еще не хватает значений
        if (values.size() < recordCount) {
            long missingValueCount = recordCount - values.size();
            long missingUniqueCount = Math.min(missingValueCount, requiredUniqueCount - objectSet.size());

            log.debug("Generating fallback values for column {}: missingValueCount = {}, missingUniqueCount = {}", columnMetadata.getName(), missingValueCount, missingUniqueCount);

            List<Object> fallbackUniqueValues = new ArrayList<>();
            for (long i = 0; i < missingUniqueCount; i++) {
                Object val = generator.generateValue();
                int attempts = 0;
                while (!objectSet.add(val) && attempts < 10000) {
                    val = generator.generateValue();
                    attempts++;
                }
                if (attempts < 10000) {
                    fallbackUniqueValues.add(val);
                }
            }

            if (!fallbackUniqueValues.isEmpty()) {
                log.debug("Filling {} missing values from {} fallback unique values for column {}", missingValueCount, fallbackUniqueValues.size(), columnMetadata.getName());
                for (long i = 0; i < missingValueCount; i++) {
                    values.add(fallbackUniqueValues.get(random.nextInt(fallbackUniqueValues.size())));
                }
            } else {
                log.warn("Warning: Could not generate fallback unique values for column {}, filling with nulls", columnMetadata.getName());
                for (long i = 0; i < missingValueCount; i++) {
                    values.add(null);
                }
            }
        }

        return values;
    }

    @Override
    public List<Object> generateValues(ColumnMetadata columnMetadata) {
        log.info("Generating values for column: {}", columnMetadata.getName());
        return generateValues(columnMetadata, columnMetadata.getRecordCount());
    }
}

