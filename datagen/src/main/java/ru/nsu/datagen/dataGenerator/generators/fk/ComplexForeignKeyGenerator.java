package ru.nsu.datagen.dataGenerator.generators.fk;

import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;

import java.util.List;
import java.util.Map;

public interface ComplexForeignKeyGenerator {
    List<List<Object>> generateForeignKeys(List<ColumnMetadata> columnMetadata,
                                     Map<String, List<Object>> allGeneratedData);
}
