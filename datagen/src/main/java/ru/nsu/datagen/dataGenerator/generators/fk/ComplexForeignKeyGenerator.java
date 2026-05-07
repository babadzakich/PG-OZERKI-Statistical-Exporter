package ru.nsu.datagen.dataGenerator.generators.fk;

import java.util.List;
import java.util.Map;

import ru.nsu.datagen.dataGenerator.generators.ColumnGenerator;

public interface ComplexForeignKeyGenerator extends ColumnGenerator {
    List<List<Object>> generateComplexForeignKeys(Map<String, List<Object>> allGeneratedData);
    
}
