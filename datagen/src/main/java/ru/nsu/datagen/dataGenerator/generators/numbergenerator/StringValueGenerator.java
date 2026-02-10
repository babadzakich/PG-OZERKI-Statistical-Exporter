package ru.nsu.datagen.dataGenerator.generators.numbergenerator;

import net.datafaker.Faker;
import lombok.extern.slf4j.Slf4j;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

@Slf4j
public class StringValueGenerator implements ValueGenerator {
    private final Faker faker;
    private final int avgSize;
    private final int maxSize;

    public StringValueGenerator(int avgSize, int maxSize) {
        this.avgSize = avgSize;
        this.maxSize = maxSize;
        this.faker = new Faker(Locale.forLanguageTag("ru"));
    }

    public StringValueGenerator(int avgSize, int maxSize, Locale locale) {
        this.avgSize = avgSize;
        this.maxSize = maxSize;
        this.faker = new Faker(locale);
    }

    @Override
    public Object generateValue() {
        return faker.lorem().characters(avgSize);
    }

    @Override
    public Object generateValue(Object leftBorder, Object rightBorder) {
        if (!(leftBorder instanceof String left) || !(rightBorder instanceof String right)) {
            throw new IllegalArgumentException("Borders must be of type String");
        }
        if (left.equals(right)) {
            return left;
        }
        int leftLength = left.length();
        int rightLength = right.length();
        String prefix = null;

        int length = Math.min(leftLength, rightLength);
        for (int i = 0; i < length; i++) {
            if (left.charAt(i) != right.charAt(i)) {
                prefix = left.substring(0, i);
                length = i;
                break;
            }
        }
        if (prefix == null) {
            prefix = left.substring(0, length);
        }

        StringBuilder result = new StringBuilder(prefix);

        if (length < leftLength || length < rightLength) {
            char leftChar = length < leftLength ? left.charAt(length) : '\u0000';
            char rightChar = length < rightLength ? right.charAt(length) : '\uffff';

            if (rightChar > leftChar + 1) {
                char randomChar = (char) (leftChar + 1 + faker.random().nextInt(rightChar - leftChar - 1));
                result.append(randomChar);
            } else {
                result.append(leftChar);
            }
        }

        int targetLength = Math.min(avgSize, maxSize);
        while (result.length() < targetLength) {
            result.append(faker.lorem().characters(avgSize - result.length()));
        }

        if (result.length() > maxSize) {
            return result.substring(0, maxSize);
        }

        return result.toString();
    }

    @Override
    public List<Object> generateValues(int count) {
        Set<String> uniqueValues = new HashSet<>();
        long maxAttempts = count * 100L;
        long attempts = 0, i = 0;

        while (i < count && attempts < maxAttempts) {
            String value = faker.lorem().characters(avgSize);
            i += uniqueValues.add(value) ? 1 : 0;
            attempts++;
        }

        return new ArrayList<>(uniqueValues);
    }

    @Override
    public List<Object> generateValues(int count, Object leftBorder, Object rightBorder) {
        if (!(leftBorder instanceof String left) || !(rightBorder instanceof String right)) {
            throw new IllegalArgumentException("Borders must be of type String");
        }

        Set<String> uniqueValues = new HashSet<>();
        int maxAttempts = count * 100;
        int attempts = 0;

        while (uniqueValues.size() < count && attempts < maxAttempts) {
            String value = (String) generateValue(left, right);
            uniqueValues.add(value);
            attempts++;
        }

        if (uniqueValues.size() < count) {
            log.warn("Could not generate {} unique string values between '{}' and '{}' after {} attempts. Generated only {} unique values.",
                    count, left, right, maxAttempts, uniqueValues.size());
        }

        return new ArrayList<>(uniqueValues);
    }
}
