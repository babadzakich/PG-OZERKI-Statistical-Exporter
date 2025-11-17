package ru.nsu.datagen.dataGenerator.generators.pk;

import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import java.util.List;

public interface PrimaryKeyGenerator {
    List<Object> generatePrimaryKeys(ColumnMetadata columnMetadata);
}