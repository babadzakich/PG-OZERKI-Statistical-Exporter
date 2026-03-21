package ru.nsu.datagen.importer.explainanalyze.filter;

import java.util.Arrays;
import java.util.List;
import java.util.Optional;

import static java.util.Optional.empty;

public class FilterParser {
    public static Optional<FilterExpression> parse(String raw) {
        if (raw == null || raw.isBlank()) return empty();

        // снимаем внешние скобки: "(status = 'Arrived'::text)" -> "status = 'Arrived'::text"
        String stripped = stripOuterParens(raw.trim());

        return Optional.of(parseExpression(stripped));
    }

    private static String stripOuterParens(String trim) {
        if (trim.startsWith("(")) trim = trim.substring(1);
        if (trim.endsWith(")")) trim = trim.substring(0, trim.length() - 1);
        return trim;
    }

    private static FilterExpression parseExpression(String expr) {
        // сначала ищем AND/OR на верхнем уровне (не внутри скобок)
        int andIdx = findTopLevelOp(expr, "AND");
        int orIdx  = findTopLevelOp(expr, "OR");

        if (andIdx != -1) {
            return splitByOp(expr, andIdx, "AND", FilterExpression.LogicOp.AND);
        }
        if (orIdx != -1) {
            return splitByOp(expr, orIdx, "OR", FilterExpression.LogicOp.OR);
        }

        // простой предикат — парсим как FilterClause
        FilterClause clause = parseClause(expr);
        return FilterExpression.builder()
                .clauses(List.of(clause))
                .children(List.of())
                .build();
    }

    private static FilterClause parseClause(String expr) {
        if (expr.matches("(?i).+\\s+IS\\s+NULL"))
            return buildNullClause(expr, FilterOp.IS_NULL);
        if (expr.matches("(?i).+\\s+IS\\s+NOT\\s+NULL"))
            return buildNullClause(expr, FilterOp.IS_NOT_NULL);

        // ANY: status = ANY ('{Arrived,Departed}'::text[])
        if (expr.contains("= ANY"))
            return parseAnyClause(expr);

//        if (expr.toUpperCase().contains("BETWEEN"))
//            return parseBetweenClause(expr);
//
//        if (expr.toUpperCase().contains(" LIKE "))
//            return parseLikeClause(expr, FilterOp.LIKE);
//        if (expr.toUpperCase().contains(" ILIKE "))
//            return parseLikeClause(expr, FilterOp.ILIKE);

        return parseSimpleClause(expr);
    }

    private static FilterClause buildNullClause(String expr, FilterOp filterOp) {
        FilterClause clause = FilterClause.builder()
                .column()
                .op(filterOp)
                .build();
        return clause;
    }

    private static FilterClause parseSimpleClause(String expr) {
        // порядок важен — сначала двухсимвольные
        String[][] ops = {{"!=", "NOT_EQUALS"}, {"<>", "NOT_EQUALS"},
                {">=", "GTE"}, {"<=", "LTE"},
                {">",  "GT"},  {"<",  "LT"},
                {"=",  "EQUALS"}};

        for (String[] op : ops) {
            int idx = expr.indexOf(op[0]);
            if (idx == -1) continue;

            String col = expr.substring(0, idx).trim();
            String val = expr.substring(idx + op[0].length()).trim();

            return FilterClause.builder()
                    .column(cleanColumn(col))
                    .op(FilterOp.valueOf(op[1]))
                    .value(parseValue(val))
                    .isLiteral(isLiteral(val))
                    .build();
        }
        throw new IllegalArgumentException("Cannot parse filter clause: " + expr);
    }

    private static FilterClause parseAnyClause(String expr) {
        // "status = ANY ('{Arrived,Departed}'::text[])"
        int eqIdx  = expr.indexOf("= ANY");
        String col = cleanColumn(expr.substring(0, eqIdx).trim());

        // вытаскиваем '{Arrived,Departed}' из скобок
        String inner = expr.substring(expr.indexOf('(') + 1, expr.lastIndexOf(')'));
        // убираем ::text[] и фигурные скобки
        inner = inner.replaceAll("::[\\w\\[\\]]+", "").trim();
        inner = inner.replaceAll("[{}']", "");

        List<String> values = Arrays.asList(inner.split(","));

        return FilterClause.builder()
                .column(col)
                .op(FilterOp.ANY)
                .value(values)
                .isLiteral(true)
                .build();
    }

    // ищем AND/OR на верхнем уровне — не внутри скобок
    private static int findTopLevelOp(String expr, String op) {
        int depth = 0;
        for (int i = 0; i < expr.length(); i++) {
            char c = expr.charAt(i);
            if (c == '(') depth++;
            else if (c == ')') depth--;
            else if (depth == 0 && expr.startsWith(" " + op + " ", i - 1)) {
                return i - 1;
            }
        }
        return -1;
    }

    // убираем ::text, ::integer и прочие касты постгреса
    private static Object parseValue(String raw) {
        String cleaned = raw.replaceAll("::[\\w\\[\\]]+", "").trim();
        // убираем кавычки если строка
        if (cleaned.startsWith("'") && cleaned.endsWith("'"))
            return cleaned.substring(1, cleaned.length() - 1);
        // число
        try { return Long.parseLong(cleaned); } catch (NumberFormatException ignored) {}
        try { return Double.parseDouble(cleaned); } catch (NumberFormatException ignored) {}
        return cleaned;
    }

    private static String cleanColumn(String col) {
        // убираем алиас таблицы: "f.status" -> "status"
        return col.contains(".") ? col.substring(col.lastIndexOf('.') + 1) : col;
    }

    private static boolean isLiteral(String val) {
        // если содержит точку и не число — скорее всего ссылка на колонку (r.departure_airport)
        String cleaned = val.replaceAll("::[\\w\\[\\]]+", "").trim();
        if (cleaned.contains(".")) {
            try { Double.parseDouble(cleaned); return true; }
            catch (NumberFormatException e) { return false; }
        }
        return true;
    }

}
