package ru.nsu.datagen.plancheck.ted;

import lombok.extern.slf4j.Slf4j;
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
@Slf4j
public class TreeEditDistance {


    private static final Map<String, Float> NODE_WEIGHTS = Map.of(
            "Seq Scan", 2.0f,
            "Index Scan", 1.5f,
            "Index Only Scan", 1.2f,
            "Hash Join", 3.0f,
            "Merge Join", 3.0f,
            "Nested Loop", 2.5f,
            "Hash", 1.5f,
            "Merge", 1.5f,
            "Aggregate", 1.0f,
            "Sort", 0.5f
    );

    public static float compute(PlanNode actualPlan, PlanNode sourcePlan) {
        if (actualPlan == null && sourcePlan == null) return 0;
        if (actualPlan == null) return costInsertTree(sourcePlan);
        if (sourcePlan == null) return costDeleteTree(actualPlan);
        return forestDistance(Collections.singletonList(actualPlan), Collections.singletonList(sourcePlan));
    }


    private static float costDeleteTree(PlanNode node) {
        float cost = getNodeWeight(node);
        for (PlanNode child : node.getPlans()) {
            cost += costDeleteTree(child);
        }
        return cost;
    }

    private static float costInsertTree(PlanNode node) {
        float cost = getNodeWeight(node);
        for (PlanNode child : node.getPlans()) {
            cost += costInsertTree(child);
        }
        return cost;
    }

    private static float costReplace(PlanNode a, PlanNode b) {
        boolean sameType = Objects.equals(a.nodeType, b.nodeType);
        boolean sameRel = Objects.equals(a.relName, b.relName);
        boolean sameIndex = Objects.equals(a.index, b.index);
        boolean sameSide = Objects.equals(a.parentRelationship, b.parentRelationship);

        if (sameType && sameRel && sameIndex && sameSide) {
            return 0f;
        }


        if (sameType) {
            float penalty = 0f;
            if (!sameRel || !sameIndex) penalty += 0.4f;
            if (!sameSide) penalty += 0.2f;
            return penalty;
        }


        float weightA = getNodeWeight(a);
        float weightB = getNodeWeight(b);

        return (weightA + weightB) * 0.4f;
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
                            forest1.get(i).getPlans(),
                            forest2.get(j).getPlans()
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


    private static float getNodeWeight(PlanNode node) {
        Float res = NODE_WEIGHTS.get(node.getNodeType());
        if (res == null) {
            log.warn("No such node type: " + node.nodeType);
            return 1.0f;
        }
        return res;
    }

    private static float totalTreeWeight(PlanNode node) {
        if (node == null) return 0;
        float weight = getNodeWeight(node);
        for (PlanNode child : node.getPlans()) {
            weight += totalTreeWeight(child);
        }
        return weight;
    }

    public static float computeSimilarity(PlanNode actualPlan, PlanNode sourcePlan) {
        float distance = compute(actualPlan, sourcePlan);

        float costActualPlan = totalTreeWeight(actualPlan);
        float costSourcePlan = totalTreeWeight(sourcePlan);

        float maxCost = costActualPlan + costSourcePlan;

        if (maxCost == 0) return 100.0f;

        float similarity = (1.0f - (distance / maxCost)) * 100.0f;

        return Math.max(0, Math.min(100, similarity));
    }
}