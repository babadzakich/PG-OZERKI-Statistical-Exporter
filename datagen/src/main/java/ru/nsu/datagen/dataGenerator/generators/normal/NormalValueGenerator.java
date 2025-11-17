package ru.nsu.datagen.dataGenerator.generators.normal;

import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import java.util.List;

public interface NormalValueGenerator {
    List<Object> generateValues(ColumnMetadata columnMetadata);
}