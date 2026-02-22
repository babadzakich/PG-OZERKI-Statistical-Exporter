package ru.nsu.datagen.dataGenerator.model;

import lombok.Getter;
import ru.nsu.datagen.dataGenerator.generators.fk.RelationshipType;
import java.util.Objects;

@Getter
public class ForeignKeyMetadata {
    private final String referencedSchema;
    private final String referencedTable;
    private final String referencedColumn;
    private final RelationshipType relationshipType;

    public ForeignKeyMetadata(String referencedSchema, String referencedTable, String referencedColumn, RelationshipType relationshipType) {
        this.referencedSchema = referencedSchema;
        this.referencedTable = referencedTable;
        this.referencedColumn = referencedColumn;
        this.relationshipType = relationshipType;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (o == null || getClass() != o.getClass()) return false;
        ForeignKeyMetadata that = (ForeignKeyMetadata) o;
        return Objects.equals(referencedTable, that.referencedTable) &&
                Objects.equals(referencedColumn, that.referencedColumn) &&
                relationshipType == that.relationshipType;
    }

    @Override
    public int hashCode() {
        return Objects.hash(referencedTable, referencedColumn, relationshipType);
    }
}