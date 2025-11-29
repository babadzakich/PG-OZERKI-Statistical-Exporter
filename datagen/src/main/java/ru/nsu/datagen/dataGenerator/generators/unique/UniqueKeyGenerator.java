package ru.nsu.datagen.dataGenerator.generators.unique;

import java.util.List;
import java.util.Map;

public interface UniqueKeyGenerator {
    void generate();
    void generate(Map<String, List<Object>> columnData);
}
