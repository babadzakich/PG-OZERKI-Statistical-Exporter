/*
 * Click nbfs://nbhost/SystemFileSystem/Templates/Licenses/license-default.txt to change this license
 * Click nbfs://nbhost/SystemFileSystem/Templates/Classes/Class.java to edit this template
 */

package ru.nsu.datagen.dataGenerator.model.batchmodel;

import java.util.ArrayList;
import java.util.List;

import lombok.Data;
import ru.nsu.datagen.dataGenerator.model.ColumnMetadata;

/**
 *
 * @author kubicl
 */
@Data
public class StateData {
    private int generatedCount;
    private final int columnsAmount;

    private double[] nullChances;

    private final List<ValuesChances> mcvChances;

    private final List<List<Double>> bucketsCounters;

    private List<List<Object>> mandatoryValues;

    public StateData() {
        this.generatedCount = 0;
        this.nullChances = new double[0];
        this.columnsAmount = 0;
        this.bucketsCounters = List.of();
        this.mcvChances = List.of();
    }

    public StateData(StateData other) {
        this.generatedCount = other.generatedCount;
        this.nullChances = other.nullChances;
        this.columnsAmount = other.columnsAmount;
        this.bucketsCounters = other.bucketsCounters;
        this.mcvChances = other.mcvChances;
        this.mandatoryValues = other.mandatoryValues;
    }

    public StateData(ColumnMetadata columnMetadata) {
        this.nullChances = new double[1];
        nullChances[0] = Math.max(0.0, Math.min(1.0, columnMetadata.getNullFrac()));
        this.generatedCount = 0;
        this.columnsAmount = 1;
        List<Object> values = columnMetadata.getMcv().keySet().stream().toList();
        List<Double> chances = new ArrayList<>();

        double lastChance = this.nullChances[0];
        for (Object value : values) {
            double chance = Math.min(1.0, lastChance + columnMetadata.getMcv().get(value));
            chances.add(chance);
            lastChance = chance;
        }
        this.mcvChances = new ArrayList<>();
        this.mcvChances.add(new ValuesChances(values, chances));

        this.bucketsCounters = new ArrayList<>();
        this.bucketsCounters.add(new ArrayList<>());
        int bucketAmount = columnMetadata.getHistogramm() == null ? 0 : columnMetadata.getHistogramm().size() - 1;
        if (bucketAmount <= 0) {
            return;
        }
        double bucketChance = Math.max(0.0, 1.0 - lastChance) / bucketAmount;
        for (int i = 0; i < bucketAmount; i++) {
            bucketsCounters.getFirst().add(lastChance + bucketChance);
            lastChance += bucketChance;
        }
    }

    public StateData(List<ColumnMetadata> columns) {
        this.nullChances = new double[columns.size()];
        this.columnsAmount = columns.size();
        this.mcvChances = new ArrayList<>();
        this.bucketsCounters = new ArrayList<>();
        for (int i = 0; i < columns.size(); i++) {
            ColumnMetadata columnMetadata = columns.get(i);
            this.nullChances[i] = Math.max(0.0, Math.min(1.0, columnMetadata.getNullFrac()));
            this.generatedCount = 0;

            List<Object> values = columnMetadata.getMcv().keySet().stream().toList();
            List<Double> chances = new ArrayList<>();

            double lastChance = this.nullChances[i];
            for (Object value : values) {
                double chance = Math.min(1.0, lastChance + columnMetadata.getMcv().get(value));
                chances.add(chance);
                lastChance = chance;
            }

            this.mcvChances.add(new ValuesChances(values, chances));

            this.bucketsCounters.add(new ArrayList<>());
            int bucketAmount = columnMetadata.getHistogramm() == null ? 0 : columnMetadata.getHistogramm().size() - 1;
            if (bucketAmount <= 0) {
                return;
            }
            double bucketChance = Math.max(0.0, 1.0 - lastChance) / bucketAmount;
            while (bucketAmount-- > 0) {
                bucketsCounters.getLast().add(lastChance + bucketChance);
                lastChance += bucketChance;
            }
        }
    }

    public void advance(int generated) {
        this.generatedCount += generated;
    }
    public void dropNull(int index) {this.nullChances[index] = 0.0;}
}
