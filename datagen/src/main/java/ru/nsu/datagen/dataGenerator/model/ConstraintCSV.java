package ru.nsu.datagen.dataGenerator.model;

import com.opencsv.bean.CsvBindByName;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

/**
 *
 * @author kubicl
 */
@Builder
@Data
@NoArgsConstructor
@AllArgsConstructor
public class ConstraintCSV {
    @CsvBindByName(column = "constraint_name")
    private String constraintName;
    @CsvBindByName(column = "type")
    private String type;
    @CsvBindByName(column = "columns")
    private String columns;
}
