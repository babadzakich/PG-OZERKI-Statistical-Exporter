package ru.nsu.datagen.dataGenerator.generators.unique;

import java.util.List;
import java.util.Map;

public interface UniqueKeyGenerator {
    void generate(Map<String, List<Object>> columnData);
    List<Object> generate();
}
