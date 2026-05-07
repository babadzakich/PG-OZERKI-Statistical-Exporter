package ru.nsu.datagen.dataGenerator.generators.fk;

import java.util.List;
import java.util.Map;

public interface ForeignKeyGenerator {
    List<Object> generateSimpleForeignKeys(Map<String, List<Object>> allGeneratedData);
}