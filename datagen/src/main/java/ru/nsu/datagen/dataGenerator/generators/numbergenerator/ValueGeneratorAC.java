package ru.nsu.datagen.dataGenerator.generators.numbergenerator;

import lombok.extern.slf4j.Slf4j;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

@Slf4j
public abstract class ValueGeneratorAC implements ValueGenerator {
    private static final long MAX_GENERATION_ATTEMPTS_MULTIPLIER = 100L;

    Object minValue;
    Object maxValue;

    protected ValueGeneratorAC() {}

    protected ValueGeneratorAC(Object minValue, Object maxValue) {
        this.minValue = minValue;
        this.maxValue = maxValue;
    }

    /**
     * Генерирует одно случайное значение
     *
     * @return сгенерированное значение
     */
    public Object generateValue() {
        return generateValue(minValue, maxValue);
    }

    /**
     * Генерирует случайное значение в заданном диапазоне
     *
     * @param leftBorder - левая граница диапазона включительно
     * @param rightBorder - правая граница диапазона исключительно
     * @return сгенерированное значение
     */
    public abstract Object generateValue(Object leftBorder, Object rightBorder);

    /**
     * Генерирует список уникальных случайных значений
     *
     * @param count - количество значений для генерации
     * @return список сгенерированных значений
     */
    public List<Object> generateValues(int count) {
        return generateValues(count, minValue, maxValue);
    }

    /**
     * Генерирует список уникальных случайных значений в заданном диапазоне
     *
     * @param count - количество значений для генерации
     * @param leftBorder - левая граница диапазона включительно
     * @param rightBorder - правая граница диапазона исключительно
     * @return список сгенерированных значений
     */
    public List<Object> generateValues(int count, Object leftBorder, Object rightBorder) {
        List<Object> values = new ArrayList<>();
        generateValues(values, count, leftBorder, rightBorder);
        return values;
    }

    /**
     * Генерирует список уникальных случайных значений и добавляет их в переданный список
     *
     * @param values - список, в который будут добавлены сгенерированные значения
     * @param count - количество значений для генерации
     */
    public void generateValues(List<Object> values, int count) {
        generateValues(values, count, minValue, maxValue);
    }

    /**
     * Генерирует список уникальных случайных значений в заданном диапазоне и добавляет их в переданный список
     *
     * @param values - список, в который будут добавлены сгенерированные значения
     * @param count - количество значений для генерации
     * @param leftBorder - левая граница диапазона включительно
     * @param rightBorder - правая граница диапазона исключительно
     */
    public void generateValues(List<Object> values, int count, Object leftBorder, Object rightBorder) {
        Set<Object> uniqValues = new HashSet<>(values);
        long maxAttempts = count * MAX_GENERATION_ATTEMPTS_MULTIPLIER;
        long attempts = 0;
        int i = 0;
        while (i < count && attempts < maxAttempts) {
            Object val = generateValue(leftBorder, rightBorder);
            if (uniqValues.add(val)) {
                values.add(val);
                i++;
            }
            attempts++;
        }
        if (values.size() < count) {
            log.error("Could not generate {} unique timestamps in range ({} to {}) after {} attempts. Generated only {} unique values.",
                    count, leftBorder, rightBorder, maxAttempts, values.size());
            throw new RuntimeException("Couldn`t generate all unique values with " + this.getClass()
                    + ". Done only " + values.size() + " out of " + count + " unique values.");
        }
    }
}
