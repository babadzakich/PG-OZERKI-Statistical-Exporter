/*
 * Click nbfs://nbhost/SystemFileSystem/Templates/Licenses/license-default.txt to change this license
 * Click nbfs://nbhost/SystemFileSystem/Templates/Classes/Record.java to edit this template
 */

package ru.nsu.datagen.dataGenerator.model.batchmodel;

import java.util.List;

/**
 *
 * @author kubicl
 */
public record HistogrammGenerationResults(int bucketIndex, int bucketGeneratedCount, int addedCount, List<Object> bucketUniqueValues) {}
