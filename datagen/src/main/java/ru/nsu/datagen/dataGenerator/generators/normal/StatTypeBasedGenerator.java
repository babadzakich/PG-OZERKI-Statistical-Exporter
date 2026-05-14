package ru.nsu.datagen.dataGenerator.generators.normal;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.ThreadLocalRandom;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGenerator;
import ru.nsu.datagen.dataGenerator.generators.numbergenerator.ValueGeneratorFactory;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import ru.nsu.datagen.dataGenerator.model.batchmodel.HistogrammGenerationResults;
import ru.nsu.datagen.dataGenerator.model.batchmodel.McvGenerationResults;
import ru.nsu.datagen.dataGenerator.model.batchmodel.StateData;

@Slf4j
public class StatTypeBasedGenerator implements NormalValueGenerator {
    private final ColumnMetadata columnMetadata;
    private final List<Object> mcvs;
    private final ThreadLocalRandom random = ThreadLocalRandom.current();
    private final ValueGenerator generator;

    public StatTypeBasedGenerator(ColumnMetadata columnMetadata) {
        this.columnMetadata = columnMetadata;
        this.mcvs = new ArrayList<>(columnMetadata.getMcv().keySet());
        this.generator = ValueGeneratorFactory.createValueGenerator(columnMetadata);
    }

    private int generateNulls(int offset, int batchSize, List<Object> values) {
        int nullCount = columnMetadata.getNullFrac() * columnMetadata.getRecordCount() > 0 ? (int) Math.round(columnMetadata.getNullFrac() * columnMetadata.getRecordCount()) : 0;
        int addedNulls = Math.min(offset + batchSize, nullCount);
        for (int i = offset; i < addedNulls; i++) {
            values.add(null);
        }
        return addedNulls - offset;
    }

    private McvGenerationResults generateMCVs(int batchSize, int currentMcvIndex, int currentMcvGeneratedCount, List<Object> values) {
        int generated = values.size();
        int added = 0;
        for (int i = currentMcvIndex; i < mcvs.size(); i++) {
            int start = 0;
            if (i == currentMcvIndex) {
                start = currentMcvGeneratedCount;
            }

            double freq = columnMetadata.getMcv().get(mcvs.get(i));
            int mcvCount = (int) Math.round(freq * columnMetadata.getRecordCount());
            
            int leftInThisMcv = mcvCount - start;
            int batchLeft = batchSize - values.size();
            int toGenerate = Math.min(leftInThisMcv, batchLeft);

            for (int j = 0; j < toGenerate; j++) {
                values.add(mcvs.get(i));
            }
            added += toGenerate;
            if (generated + added >= batchSize) {
                if (start + toGenerate >= mcvCount) {
                    return new McvGenerationResults(i + 1, 0, added);
                }
                return new McvGenerationResults(i, start + toGenerate, added);
            }
        }
        return new McvGenerationResults(mcvs.size(), 0, added);
    }
    
    

    private int getMcvValueCount() {
        return columnMetadata.getMcv().values().stream()
                .mapToInt(freq -> (int) Math.round(freq * columnMetadata.getRecordCount()))
                .sum();
    }

    private HistogrammGenerationResults generateByHistogramm(int batchSize, int currentBucketIndex, int currentBucketGeneratedCount, List<Object> currentBucketUniqueValues, List<Object> values) {
        int generated = values.size();
        int added = 0;
        int histogramBuckets = columnMetadata.getHistogramm().size() - 1;
        int remainingUniqueCount = columnMetadata.getNdistinct() - mcvs.size() - (columnMetadata.getNullFrac() * columnMetadata.getRecordCount() > 0 ? 1 : 0);
        int remainingValueCount = columnMetadata.getRecordCount() - (int) Math.round(columnMetadata.getNullFrac() * columnMetadata.getRecordCount()) - getMcvValueCount();

        for (int i = currentBucketIndex; i < histogramBuckets; i++) {
            // Распределяем общее количество значений равномерно по бакетам с учётом остатка
            long baseValueCount = remainingValueCount / histogramBuckets;
            long valueRemainder = remainingValueCount % histogramBuckets;
            long rangeValueCount = Math.min(baseValueCount + (i < valueRemainder ? 1 : 0), remainingValueCount);

            if (i == currentBucketIndex && currentBucketGeneratedCount > 0) {
                int bucketLeft = (int) Math.max(0, rangeValueCount - currentBucketGeneratedCount);
                int batchLeft = batchSize - values.size();
                int toGenerate = Math.min(bucketLeft, batchLeft);
                int addedInThisBucket = 0;

                if (currentBucketGeneratedCount < currentBucketUniqueValues.size()) {
                    int uniqueLeft = currentBucketUniqueValues.size() - currentBucketGeneratedCount;
                    int uniqueToGenerate = Math.min(toGenerate, uniqueLeft);
                    log.trace("Resuming bucket {}: Filling {} values from unique values", i, uniqueToGenerate);
                    for (int j = 0; j < uniqueToGenerate; j++) {
                        values.add(currentBucketUniqueValues.get(currentBucketGeneratedCount + j));
                    }
                    addedInThisBucket += uniqueToGenerate;
                    toGenerate -= uniqueToGenerate;
                }
                for (int j = 0; j < toGenerate; j++) {
                    values.add(currentBucketUniqueValues.get(random.nextInt(currentBucketUniqueValues.size())));
                }
                addedInThisBucket += toGenerate;
                added += addedInThisBucket;

                int nextBucketGeneratedCount = currentBucketGeneratedCount + addedInThisBucket;
                if (generated + added >= batchSize || nextBucketGeneratedCount < rangeValueCount) {
                    return new HistogrammGenerationResults(i, nextBucketGeneratedCount, added, currentBucketUniqueValues);
                }
                continue;
            }
            Object lowerBound = columnMetadata.getHistogramm().get(i);
            Object upperBound = columnMetadata.getHistogramm().get(i + 1);
            log.trace("Bucket {}: Lower bound = {}, Upper bound = {}", i, lowerBound, upperBound);

            // Распределяем уникальные значения равномерно по бакетам с учётом остатка
            // Например: 2002 значения на 100 бакетов = 20 + (1 если i < 2), т.е. первые 2 бакета по 21, остальные по 20
            long baseUniqueCount = remainingUniqueCount / histogramBuckets;
            long uniqueRemainder = remainingUniqueCount % histogramBuckets;
            long rangeUniqueCount = baseUniqueCount + (i < uniqueRemainder ? 1 : 0);

            // Генерируем уникальные значения для данного бакета
            Set<Object> bucketUniqueValues = new HashSet<>();
            bucketUniqueValues.add(lowerBound);
            bucketUniqueValues.add(upperBound);

            for (long j = 2; j < rangeUniqueCount; j++) {
                Object val = generator.generateValue(lowerBound, upperBound);
                int attempts = 0;
                while (!columnMetadata.getMcv().keySet().contains(val) && !bucketUniqueValues.add(val) && attempts < 10000) {
                    val = generator.generateValue(lowerBound, upperBound);
                    attempts++;
                }
                if (attempts < 10000) {
                    bucketUniqueValues.add(val);
                }
            }
            log.trace("Bucket {}: Generated {} unique values: {}", i, bucketUniqueValues.size(), bucketUniqueValues);
            List<Object> bucketUniqueValuesList = new ArrayList<>(bucketUniqueValues);
            // Заполняем values случайными значениями из сгенерированных уникальных
            if (!bucketUniqueValuesList.isEmpty()) {
                int bucketLeft = (int) rangeValueCount;
                int batchLeft = batchSize - values.size();
                int toGenerate = Math.min(bucketLeft, batchLeft);
                int addedToBucket = 0;

                log.trace("Bucket {}: Filling {} values from unique values", i, rangeValueCount);
                for (int j = i == 0 ? 0 : 1; j < bucketUniqueValuesList.size() && j < toGenerate; j++) {
                    values.add(bucketUniqueValuesList.get(j));
                    addedToBucket++;
                }
                if (addedToBucket < bucketUniqueValuesList.size()) {
                    return new HistogrammGenerationResults(i, addedToBucket, addedToBucket, bucketUniqueValuesList);
                }
                toGenerate -= addedToBucket;
                log.trace("Bucket {}: Added {} unique values, now filling remaining {} values", i, rangeUniqueCount, rangeValueCount - rangeUniqueCount);
                for (long j = 0; j < toGenerate; j++) {
                    values.add(bucketUniqueValuesList.get(random.nextInt(bucketUniqueValuesList.size())));
                    addedToBucket++;
                }
                if (addedToBucket < rangeValueCount) {
                    return new HistogrammGenerationResults(i, addedToBucket, addedToBucket, bucketUniqueValuesList);
                }
                added += addedToBucket;
                log.trace("Bucket {}: Filled {} values", i, rangeValueCount);
            } else {
                log.warn("Warning: Could not generate unique values for bucket {}", i);
            }
        }
        return new HistogrammGenerationResults(histogramBuckets, 0, added, List.of());
    }

    private int generateRemainingValues(int batchSize, StateData stateData, List<Object> values) {
        int generated = 0;
        int remainingInTable = columnMetadata.getRecordCount() - stateData.getGeneratedCount() - values.size();
        int toGenerate = Math.min(batchSize - values.size(), Math.max(0, remainingInTable));
        

        for (int i = 0; i < toGenerate; i++) {
            values.add(generator.generateValue());
            generated++;
        }

        return generated;
    }

    @Override
    public List<List<Object>> generateValues(int batchSize, StateData stateData) {
        List<Object> values = new ArrayList<>();
        int remaining = columnMetadata.getRecordCount() - stateData.getGeneratedCount();
        int toGenerate = Math.min(batchSize, Math.max(0, remaining));
        
        for (int i = 0; i < toGenerate; i++) {
            double r = random.nextDouble();
            if (r < stateData.getNullChances()[0]) {
                values.add(null);
            } else if (!stateData.getMcvChances().getFirst().chances().isEmpty() && (r < stateData.getMcvChances().getFirst().chances().getLast())) {
                for (int j = 0; j < stateData.getMcvChances().getFirst().chances().size(); j++) {
                    if (r < stateData.getMcvChances().getFirst().chances().get(j)) {
                        values.add(stateData.getMcvChances().getFirst().values().get(j));
                        break;
                    }
                }
            } else {
                if (columnMetadata.getHistogramm() != null && columnMetadata.getHistogramm().size() > 1) {
                    int bucketIndex = 0;
                    while (bucketIndex < stateData.getBucketsCounters().getFirst().size() && r >= stateData.getBucketsCounters().getFirst().get(bucketIndex)) {
                        bucketIndex++;
                    }
                    bucketIndex = Math.min(bucketIndex, columnMetadata.getHistogramm().size() - 2);

                    Object left = columnMetadata.getHistogramm().get(bucketIndex);
                    Object right = columnMetadata.getHistogramm().get(bucketIndex + 1);
                    values.add(generateHistogramValue(left, right));
                } else {
                    values.add(generator.generateValue());
                }
            }
        }

        stateData.advance(values.size());
        return List.of(values);
    }

    @Override
    public List<Object> generateValues() {
        log.info("Generating values for column: {}", columnMetadata.getName());
        StateData stateData = new StateData(columnMetadata);
        return generateValues(columnMetadata.getRecordCount(), stateData).getFirst();
    }

    private Object generateHistogramValue(Object left, Object right) {
        Object value = generator.generateValue(left, right);
        int attempts = 0;
        while (columnMetadata.getMcv().containsKey(value) && attempts < 100) {
            value = generator.generateValue(left, right);
            attempts++;
        }
        return value;
    }
}
