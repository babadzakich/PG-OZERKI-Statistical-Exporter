package ru.nsu.datagen.plancheck.ted;

import ru.nsu.datagen.plancheck.struct.PlanNode;

import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Objects;

/* TODO: склеить тип ноды с полем, отвечающим за имя отношения, параметры джойна и тд (Seq -> Seq(MyTable))
    тогда можно +- однозначно идентифицировать узлы и это буде относительно точно
 */

/** Zhang-Shasha impl
 учитывается только тип узла
 */
public class TreeEditDistance {
    float deleteCost = 1f;
    float relable = 1f;
    float insert = 1f;

    private static final Map<String, Float> NODE_WEIGHTS = Map.of(
            "Seq Scan", 2.0f,
            "Index Scan", 1.5f,
            "Index Only Scan", 1.2f,
            "Hash Join", 3.0f,
            "Merge Join", 3.0f,
            "Nested Loop", 2.5f,
            "Aggregate", 1.0f,
            "Sort", 0.5f
    );

    public static float compute(PlanNode tree1, PlanNode tree2) {
        if (tree1 == null && tree2 == null) return 0;
        if (tree1 == null) return costInsertTree(tree2);
        if (tree2 == null) return costDeleteTree(tree1);
        return forestDistance(Collections.singletonList(tree1), Collections.singletonList(tree2));
    }


    private static float costDeleteTree(PlanNode node) {
        float cost = getNodeWeight(node);
        for (PlanNode child : safeGetPlans(node)) {
            cost += costDeleteTree(child);
        }
        return cost;
    }

    private static float costInsertTree(PlanNode node) {
        float cost = getNodeWeight(node);
        for (PlanNode child : safeGetPlans(node)) {
            cost += costInsertTree(child);
        }
        return cost;
    }

    private static float costReplace(PlanNode a, PlanNode b) {
        if (a.nodeType.equals(b.nodeType)) {
            if (a.relName.equals(b.relName) && Objects.equals(a.index, b.index)) {
                return 0f;
            }
            return 0.5f;
        }

        return (getNodeWeight(a) + getNodeWeight(b)) * 0.4f;
    }

    private static float forestDistance(List<PlanNode> forest1, List<PlanNode> forest2) {
        int n = forest1.size();
        int m = forest2.size();
        float[][] dp = new float[n + 1][m + 1];

        // Заполняем таблицу снизу вверх
        for (int i = n; i >= 0; i--) {
            for (int j = m; j >= 0; j--) {
                if (i == n && j == m) {
                    dp[i][j] = 0;
                } else if (i == n) {
                    // Вставка оставшихся из forest2
                    float cost = 0;
                    for (int k = j; k < m; k++) {
                        cost += costInsertTree(forest2.get(k));
                    }
                    dp[i][j] = cost;
                } else if (j == m) {
                    // Удаление оставшихся из forest1
                    float cost = 0;
                    for (int k = i; k < n; k++) {
                        cost += costDeleteTree(forest1.get(k));
                    }
                    dp[i][j] = cost;
                } else {
                    float deleteOption = dp[i + 1][j] + costDeleteTree(forest1.get(i));
                    float insertOption = dp[i][j + 1] + costInsertTree(forest2.get(j));
                    float childrenDist = forestDistance(
                            safeGetPlans(forest1.get(i)),
                            safeGetPlans(forest2.get(j))
                    );
                    float replaceOption = childrenDist
                            + costReplace(forest1.get(i), forest2.get(j))
                            + dp[i + 1][j + 1];

                    dp[i][j] = Math.min(deleteOption, Math.min(insertOption, replaceOption));
                }
            }
        }
        return dp[0][0];
    }

    private static List<PlanNode> safeGetPlans(PlanNode node) {
        if (node == null || node.plans == null) {
            return Collections.emptyList();
        }
        return node.plans;
    }

    private static float getNodeWeight(PlanNode node) {
        return NODE_WEIGHTS.get(node.getNodeType());
    }

    private static float totalTreeWeight(PlanNode node) {
        if (node == null) return 0;
        float weight = getNodeWeight(node);
        for (PlanNode child : safeGetPlans(node)) {
            weight += totalTreeWeight(child);
        }
        return weight;
    }

    public static float computeSimilarity(PlanNode tree1, PlanNode tree2) {
        float distance = compute(tree1, tree2);

        float costTree1 = totalTreeWeight(tree1);
        float costTree2 = totalTreeWeight(tree2);

        float maxCost = costTree1 + costTree2;

        if (maxCost == 0) return 100.0f;

        float similarity = (1.0f - (distance / maxCost)) * 100.0f;

        return Math.max(0, Math.min(100, similarity));
    }
}