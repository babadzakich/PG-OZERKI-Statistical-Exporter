package ru.nsu.datagen.dataGenerator.generators.unique;

import java.util.List;
import java.util.Map;

import ru.nsu.datagen.dataGenerator.generators.ColumnGenerator;

public interface UniqueKeyGenerator extends ColumnGenerator {
    void generate(Map<String, List<Object>> columnData);
    void generate(Map<String, List<Object>> columnData, int offset, int batchSize);
    List<Object> generate();
}
