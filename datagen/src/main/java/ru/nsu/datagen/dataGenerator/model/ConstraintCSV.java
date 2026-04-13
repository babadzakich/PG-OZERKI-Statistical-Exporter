package ru.nsu.datagen.dataGenerator.model;

import com.opencsv.bean.CsvBindByName;

import lombok.Builder;
import lombok.Data;

/**
 *
 * @author kubicl
 */
@Builder
@Data
public class ConstraintCSV {
    @CsvBindByName(column = "constraint_name")
    private final String constraintName;
    @CsvBindByName(column = "type")
    private final String type;
    @CsvBindByName(column = "columns")
    private final String columns;

    public ConstraintCSV(String constraintName, String type, String columns) {
        this.constraintName = constraintName;
        this.type = type;
        this.columns = columns;
    }
}
