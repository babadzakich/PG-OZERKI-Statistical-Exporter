package ru.nsu.datagen.importer.explainanalyze.filter;

import java.util.Arrays;
import java.util.List;
import java.util.Optional;

public class FilterParser {
    public static Optional<FilterExpression> parse(String raw) {
        if (raw == null || raw.isBlank()) return Optional.empty();
        try { return Optional.of(parseExpression(raw.trim())); }
        catch (IllegalArgumentException unsupported) { return Optional.empty(); }
    }

    private static FilterExpression parseExpression(String raw) {
        String expr = stripOuterParens(raw.trim());
        int idx = findTopLevel(expr, "OR");
        if (idx >= 0) return binary(expr, idx, "OR", FilterExpression.LogicOp.OR);
        idx = findTopLevel(expr, "AND");
        if (idx >= 0) return binary(expr, idx, "AND", FilterExpression.LogicOp.AND);
        FilterClause clause = parseClause(expr);
        return FilterExpression.builder().clauses(List.of(clause)).children(List.of()).build();
    }

    private static FilterExpression binary(String expr, int idx, String op, FilterExpression.LogicOp logic) {
        int rightStart = idx + op.length();
        return FilterExpression.builder().clauses(List.of())
                .children(List.of(parseExpression(expr.substring(0, idx)), parseExpression(expr.substring(rightStart))))
                .logicOp(logic).build();
    }

    private static FilterClause parseClause(String expr) {
        if (expr.matches("(?is).+\\s+IS\\s+NOT\\s+NULL")) return nullClause(expr, FilterOp.IS_NOT_NULL);
        if (expr.matches("(?is).+\\s+IS\\s+NULL")) return nullClause(expr, FilterOp.IS_NULL);
        if (expr.matches("(?is)^NOT\\b.*")) throw new IllegalArgumentException("NOT unsupported");
        if (expr.matches("(?is).*=\\s*ANY\\s*\\(.*\\)")) return parseAny(expr);
        String[][] ops = {{"!=", "NOT_EQUALS"}, {"<>", "NOT_EQUALS"}, {">=", "GTE"}, {"<=", "LTE"}, {"=", "EQUALS"}, {">", "GT"}, {"<", "LT"}};
        for (String[] item : ops) {
            int idx = findOutsideQuotes(expr, item[0]);
            if (idx < 1) continue;
            String col = expr.substring(0, idx).trim(), raw = expr.substring(idx + item[0].length()).trim();
            if (col.isEmpty() || raw.isEmpty()) break;
            Object value = parseValue(raw);
            return FilterClause.builder().column(cleanColumn(col)).op(FilterOp.valueOf(item[1]))
                    .value(value).isLiteral(isLiteral(raw)).build();
        }
        throw new IllegalArgumentException("Unsupported filter: " + expr);
    }

    private static FilterClause nullClause(String expr, FilterOp op) {
        String col = expr.replaceFirst("(?is)\\s+IS\\s+(NOT\\s+)?NULL$", "").trim();
        return FilterClause.builder().column(cleanColumn(col)).op(op).isLiteral(true).build();
    }

    private static FilterClause parseAny(String expr) {
        int idx = expr.toUpperCase().indexOf("= ANY");
        String col = cleanColumn(expr.substring(0, idx).trim());
        String inner = expr.substring(expr.indexOf('(', idx) + 1, expr.lastIndexOf(')')).replaceAll("::[\\w\\[\\]]+", "").trim();
        if (inner.startsWith("'{") && inner.endsWith("}'")) inner = inner.substring(2, inner.length() - 2);
        else throw new IllegalArgumentException("Unsupported ANY value");
        return FilterClause.builder().column(col).op(FilterOp.ANY).value(Arrays.asList(inner.split(","))).isLiteral(true).build();
    }

    private static String stripOuterParens(String s) {
        while (s.startsWith("(") && matchingClose(s, 0) == s.length() - 1) s = s.substring(1, s.length() - 1).trim();
        return s;
    }

    private static int matchingClose(String s, int start) {
        int depth = 0; boolean quote = false;
        for (int i = start; i < s.length(); i++) { char c = s.charAt(i); if (c == '\'' && (i == 0 || s.charAt(i-1) != '\\')) quote = !quote; if (!quote) { if (c == '(') depth++; if (c == ')' && --depth == 0) return i; } }
        return -1;
    }

    private static int findTopLevel(String s, String op) {
        int depth = 0; boolean quote = false;
        for (int i=0;i<s.length();i++) { char c=s.charAt(i); if(c=='\'' && (i==0||s.charAt(i-1)!='\\')) quote=!quote; if(quote) continue;
            if(c=='(') depth++; else if(c==')') depth--; else if(depth==0 && i>0 && i+op.length()<s.length() && Character.isWhitespace(s.charAt(i-1)) && s.regionMatches(true,i,op,0,op.length()) && Character.isWhitespace(s.charAt(i+op.length()))) return i;
        } return -1;
    }
    private static int findOutsideQuotes(String s, String token) { boolean quote=false; for(int i=0;i<=s.length()-token.length();i++){char c=s.charAt(i);if(c=='\'') quote=!quote; if(!quote && s.startsWith(token,i)) return i;} return -1; }
    private static Object parseValue(String raw) { String s=raw.replaceAll("::[\\w\\[\\]]+", "").trim(); if(s.startsWith("'")&&s.endsWith("'")) return s.substring(1,s.length()-1).replace("''", "'"); try{return Long.parseLong(s);}catch(NumberFormatException ignored){} try{return Double.parseDouble(s);}catch(NumberFormatException ignored){} if(s.matches("[\\w$]+\\.[\\w$]+")) throw new IllegalArgumentException("Column reference unsupported"); return s; }
    private static String cleanColumn(String col) { return col.contains(".") ? col.substring(col.lastIndexOf('.')+1) : col; }
    private static boolean isLiteral(String raw) { try { parseValue(raw); return true; } catch (IllegalArgumentException e) { return false; } }
}
