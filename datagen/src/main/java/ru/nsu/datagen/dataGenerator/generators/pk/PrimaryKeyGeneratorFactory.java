package ru.nsu.datagen.dataGenerator.generators.pk;

import ru.nsu.datagen.dataGenerator.generators.pk.impl.IntegerPrimaryKeyGenerator;
import ru.nsu.datagen.dataGenerator.generators.pk.impl.LongPrimaryKeyGenerator;
import ru.nsu.datagen.dataGenerator.generators.pk.impl.StringPrimaryKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import java.util.HashMap;
import java.util.Map;

public class PrimaryKeyGeneratorFactory {
    private final Map<String, PrimaryKeyGenerator> generators;

    public PrimaryKeyGeneratorFactory() {
        this.generators = new HashMap<>();
        registerDefaultGenerators();
    }

    private void registerDefaultGenerators() {
        generators.put("integer", new IntegerPrimaryKeyGenerator());
        generators.put("bigint", new LongPrimaryKeyGenerator());
        generators.put("varchar", new StringPrimaryKeyGenerator());
        generators.put("text", new StringPrimaryKeyGenerator());
        generators.put("char", new StringPrimaryKeyGenerator());
    }

    public void registerGenerator(String dataType, PrimaryKeyGenerator generator) {
        generators.put(dataType.toLowerCase(), generator);
    }

    public PrimaryKeyGenerator getGenerator(ColumnMetadata column) {
        String dataType = column.getDataType().toLowerCase();
        // TODO : разобраться с char(n)
        PrimaryKeyGenerator generator = generators.get(dataType);
        if (generator == null) { generator = generators.get(dataType.substring(0, 4)); }
        if (generator == null) {
            throw new IllegalArgumentException("No PK generator for data type: " + dataType);
        }

        return generator;
    }
}