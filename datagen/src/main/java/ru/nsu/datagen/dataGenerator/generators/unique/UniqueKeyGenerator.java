package ru.nsu.datagen.dataGenerator.generators.unique;

import java.util.List;
import java.util.Map;

import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;

public interface UniqueKeyGenerator {
    void generate();
    void generate(Map<String, List<Object>> columnData);
}
