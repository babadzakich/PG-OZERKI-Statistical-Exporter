package ru.nsu.datagen.dataGenerator.model;

import java.util.Objects;
import java.util.Map;

import lombok.Builder;
import lombok.Getter;

@Builder
public class ColumnMetadata {
    private final String name;
    private final String dataType;
    private final String sourceDataType;
    private final boolean isPrimaryKey;
    private final boolean isForeignKey;
    private final boolean isUnique;
    private final double nullPercentage;
    private final int recordCount;
    private final Integer maxLength;
    private final ForeignKeyMetadata foreignKeyMetadata;
    @Getter private final Map<Object, Double> mvc;
    @Getter private final int avgTupleSize;
    @Getter private final double ndistinct;
    private final boolean isArray;

    public ColumnMetadata(String name, String dataType, String sourceDataType, boolean isPrimaryKey,
                          boolean isForeignKey, boolean isUnique, double nullPercentage,
                          int recordCount, Integer maxLength, ForeignKeyMetadata foreignKeyMetadata,
                          Map<Object, Double> mvc, int avgTupleSize, double ndistinct, boolean isArray) {
        this.name = name;
        this.isPrimaryKey = isPrimaryKey;
        this.sourceDataType = dataType;
        this.isForeignKey = isForeignKey;
        this.isUnique = isUnique;
        this.nullPercentage = nullPercentage;
        this.recordCount = recordCount;
        this.maxLength = maxLength;
        this.foreignKeyMetadata = foreignKeyMetadata;
        this.isArray = dataType.contains("[") && !dataType.contains("char");
        this.mvc = mvc;
        this.avgTupleSize = avgTupleSize;
        this.ndistinct = ndistinct;

        if (isArray) {
            char dataTypeCharArray[] = dataType.toCharArray();
            StringBuilder dataTypeBuilder = new StringBuilder();
            for (int i = 0; i < dataTypeCharArray.length; i++) {
                if (dataTypeCharArray[i] == '[') {
                    break;
                }
                dataTypeBuilder.append(dataTypeCharArray[i]);
            }
            this.dataType = dataTypeBuilder.toString();
        } else {
            this.dataType = dataType;
        }

    }

    public String getName() { return name; }
    public String getSourceDataType() { return sourceDataType; }
    public String getDataType() { return dataType; }
    public boolean isPrimaryKey() { return isPrimaryKey; }
    public boolean isForeignKey() { return isForeignKey; }
    public boolean isUnique() { return isUnique; }
    public double getNullPercentage() { return nullPercentage; }
    public int getRecordCount() {return recordCount; }
    public Integer getMaxLength() { return maxLength; }
    public ForeignKeyMetadata getForeignKeyMetadata() { return foreignKeyMetadata; }
    public boolean getIsArray() { return isArray; }

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