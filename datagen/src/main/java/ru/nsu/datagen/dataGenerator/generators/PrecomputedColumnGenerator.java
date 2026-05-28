package ru.nsu.datagen.dataGenerator.generators;

import java.util.ArrayList;
import java.util.List;

import ru.nsu.datagen.dataGenerator.model.batchmodel.StateData;

public class PrecomputedColumnGenerator implements ColumnGenerator {
    private final List<List<Object>> values;

    public PrecomputedColumnGenerator(List<List<Object>> values) {
        this.values = values;
    }

    @Override
    public List<List<Object>> generateValues(int batchSize, StateData stateData) {
        List<List<Object>> batch = new ArrayList<>(values.size());
        int offset = stateData.getGeneratedCount();
        int generated = 0;

        for (List<Object> columnValues : values) {
            int end = Math.min(offset + batchSize, columnValues.size());
            List<Object> columnBatch = offset >= end
                    ? List.of()
                    : new ArrayList<>(columnValues.subList(offset, end));
            generated = Math.max(generated, columnBatch.size());
            batch.add(columnBatch);
        }

        stateData.advance(generated);
        return batch;
    }
}
