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
    private double nullChance;

    private final int mcvAmount;
    private final List<Double> mcvChances;
    private final List<Object> mcvValues;

    private final List<Double> bucketsCounters;

    public StateData() {
        this.generatedCount = 0;
        this.nullChance = 0.0;
        this.mcvAmount = 0;
        this.bucketsCounters = List.of();
        this.mcvChances = List.of();
        this.mcvValues = List.of();
    }

    public StateData(StateData other) {
        this.generatedCount = other.generatedCount;
        this.nullChance = other.nullChance;
        this.mcvAmount = other.mcvAmount;
        this.bucketsCounters = other.bucketsCounters;
        this.mcvChances = other.mcvChances;
        this.mcvValues = other.mcvValues;
    }

    public StateData(ColumnMetadata columnMetadata) {
        this.nullChance = Math.max(0.0, Math.min(1.0, columnMetadata.getNullFrac()));
        this.generatedCount = 0;
        this.mcvAmount = columnMetadata.getMcv().size();
        this.mcvChances = new ArrayList<>();
        this.mcvValues = columnMetadata.getMcv().keySet().stream().toList();
        double lastChance = this.nullChance;
            for (int i = 0; i < mcvAmount; i++) {
                double chance = Math.min(1.0, lastChance + columnMetadata.getMcv().get(mcvValues.get(i)));
                mcvChances.add(chance);
                lastChance = chance;
            }
        
        this.bucketsCounters = new ArrayList<>();
        int bucketAmount = columnMetadata.getHistogramm() == null ? 0 : columnMetadata.getHistogramm().size() - 1;
        if (bucketAmount <= 0) {
            return;
        }
        double bucketChance = Math.max(0.0, 1.0 - lastChance) / bucketAmount;
        for (int i = 0; i < bucketAmount; i++) {
            bucketsCounters.add(lastChance + bucketChance);
            lastChance += bucketChance;
        }
    }

    public void advance(int generated) {
        this.generatedCount += generated;
    }
    public void dropNull() {this.nullChance = 0.0;}
}
