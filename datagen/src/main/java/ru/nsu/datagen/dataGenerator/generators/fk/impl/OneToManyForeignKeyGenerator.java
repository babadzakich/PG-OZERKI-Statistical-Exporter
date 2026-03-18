package ru.nsu.datagen.dataGenerator.generators.fk.impl;

import lombok.extern.slf4j.Slf4j;
import ru.nsu.datagen.dataGenerator.generators.fk.ForeignKeyGenerator;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;
import java.util.*;
import java.util.concurrent.ThreadLocalRandom;

@Slf4j
public class OneToManyForeignKeyGenerator implements ForeignKeyGenerator {
    private final ThreadLocalRandom random = ThreadLocalRandom.current();
    
    public List<Object> generateForeignKeys(ColumnMetadata columnMetadata,
                                                  Map<String, List<Object>> referencedData,
                                                  Map<String, List<Object>> allGeneratedData) {
        log.info("Generating database 1-M foreign keys for columns {}", columnMetadata.getName());
        String refSchema = columnMetadata.getForeignKeyMetadata().getFirst().getReferencedSchema();
        String refTable = columnMetadata.getForeignKeyMetadata().getFirst().getReferencedTable();
        String refColumn = columnMetadata.getForeignKeyMetadata().getFirst().getReferencedColumn();
        String refKey = refSchema + '.' + refTable + '.' + refColumn;
        Set<Object> usefulData = new HashSet<>(allGeneratedData.get(refKey));

        for (int i = 1; i < columnMetadata.getForeignKeyMetadata().size(); i++) {
            refSchema = columnMetadata.getForeignKeyMetadata().get(i).getReferencedSchema();
            refTable = columnMetadata.getForeignKeyMetadata().get(i).getReferencedTable();
            refColumn = columnMetadata.getForeignKeyMetadata().get(i).getReferencedColumn();
            usefulData.retainAll(allGeneratedData.get(refSchema + '.' + refTable + '.' + refColumn));
        }

        int recordCount = columnMetadata.getRecordCount();
        List<Object> foreignKeys = new ArrayList<>();

        columnMetadata.getMcv().forEach((mcv, freq) -> {
            usefulData.remove(mcv);
            long mvcCount = Math.round(freq * columnMetadata.getRecordCount());
            for (long j = 0; j < mvcCount; j++) {
                foreignKeys.add(mcv);
            }
            log.trace("Adding MVC value: {} with frequency: {} resulting in count: {}", mcv, freq, mvcCount);
        });

        double nullPercentage = columnMetadata.getNullPercentage();
        for (int i = 0; i < recordCount * (nullPercentage/100.0); i++) {
            foreignKeys.add(null);
        }

        if (nullPercentage > 0) {
            usefulData.remove(null);
        }

        List<Object> refValues = new ArrayList<>(usefulData);

        for (int i = foreignKeys.size(); i < recordCount; i++) {
            foreignKeys.add(refValues.get(random.nextInt(refValues.size())));
        }

        return foreignKeys;
    }
}