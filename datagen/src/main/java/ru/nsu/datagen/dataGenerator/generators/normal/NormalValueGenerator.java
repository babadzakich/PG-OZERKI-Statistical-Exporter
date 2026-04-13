package ru.nsu.datagen.dataGenerator.generators.normal;

import java.util.List;

import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;

public interface NormalValueGenerator {
    List<Object> generateValues(ColumnMetadata columnMetadata);
    List<Object> generateValues(ColumnMetadata columnMetadata, int batchSize);
}