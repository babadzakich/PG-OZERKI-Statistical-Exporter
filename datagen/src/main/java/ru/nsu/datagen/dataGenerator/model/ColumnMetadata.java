package ru.nsu.datagen.dataGenerator.model;

import java.util.Objects;
import java.util.Map;

import lombok.Builder;
import lombok.Getter;

@Builder
public class ColumnMetadata {
    private final String name;
    private final String dataType;
    private final boolean isPrimaryKey;
    private final boolean isForeignKey;
    private final boolean isUnique;
    private final double nullPercentage;
    private final int recordCount;
    private final Integer maxLength;
    private final ForeignKeyMetadata foreignKeyMetadata;
    @Getter private final Map<String, Double> mvc;
    @Getter private final int avgTupleSize;

    public ColumnMetadata(String name, String dataType, boolean isPrimaryKey,
                          boolean isForeignKey, boolean isUnique, double nullPercentage,
                          int recordCount, Integer maxLength, ForeignKeyMetadata foreignKeyMetadata,
                          Map<String, Double> mvc, int avgTupleSize) {
        this.name = name;
        this.dataType = dataType;
        this.isPrimaryKey = isPrimaryKey;
        this.isForeignKey = isForeignKey;
        this.isUnique = isUnique;
        this.nullPercentage = nullPercentage;
        this.recordCount = recordCount;
        this.maxLength = maxLength;
        this.foreignKeyMetadata = foreignKeyMetadata;
        this.mvc = mvc;
        this.avgTupleSize = avgTupleSize;
    }

    public String getName() { return name; }
    public String getDataType() { return dataType; }
    public boolean isPrimaryKey() { return isPrimaryKey; }
    public boolean isForeignKey() { return isForeignKey; }
    public boolean isUnique() { return isUnique; }
    public double getNullPercentage() { return nullPercentage; }
    public int getRecordCount() {return recordCount; }
    public Integer getMaxLength() { return maxLength; }
    public ForeignKeyMetadata getForeignKeyMetadata() { return foreignKeyMetadata; }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (o == null || getClass() != o.getClass()) return false;
        ColumnMetadata that = (ColumnMetadata) o;
        return Objects.equals(name, that.name);
    }

    @Override
    public int hashCode() {
        return Objects.hash(name);
    }
}