package ru.nsu.datagen.plancheck.ted;

import ru.nsu.datagen.plancheck.struct.PlanNode;

import java.util.Collections;
import java.util.List;

/* TODO: склеить тип ноды с полем, отвечающим за имя отношения, параметры джойна и тд (Seq -> Seq(MyTable))
    тогда можно +- однозначно идентифицировать узлы и это буде относительно точно
 */

/** Zhang-Shasha impl
 учитывается только тип узла
 */
public class TreeEditDistance {
    static double deleteCost = 1f;
    static double relable = 1f;
    static double insert = 1f;

    public static double compute(PlanNode actualPlan, PlanNode sourcePlan) {
        if (actualPlan == null && sourcePlan == null) return 0;
        if (actualPlan == null) return costInsertTree(sourcePlan);
        if (sourcePlan == null) return costDeleteTree(actualPlan);
        return forestDistance(Collections.singletonList(actualPlan), Collections.singletonList(sourcePlan));
    }


    private static double costDeleteTree(PlanNode node) {
        double cost = deleteCost;
        for (PlanNode child : node.getPlans()) {
            cost += costDeleteTree(child);
        }
        return cost;
    }

    private static double costInsertTree(PlanNode node) {
        double cost = insert;
        for (PlanNode child : node.getPlans()) {
            cost += costInsertTree(child);
        }
        return cost;
    }

    private static double costReplace(PlanNode a, PlanNode b) {
        return a.getFieldSum().equals(b.getFieldSum()) ? 0f : relable;
    }

    private static double forestDistance(List<PlanNode> forest1, List<PlanNode> forest2) {
        int n = forest1.size();
        int m = forest2.size();
        double[][] dp = new double[n + 1][m + 1];

        // Заполняем таблицу снизу вверх
        for (int i = n; i >= 0; i--) {
            for (int j = m; j >= 0; j--) {
                if (i == n && j == m) {
                    dp[i][j] = 0;
                } else if (i == n) {
                    // Вставка оставшихся из forest2
                    double cost = 0;
                    for (int k = j; k < m; k++) {
                        cost += costInsertTree(forest2.get(k));
                    }
                    dp[i][j] = cost;
                } else if (j == m) {
                    // Удаление оставшихся из forest1
                    double cost = 0;
                    for (int k = i; k < n; k++) {
                        cost += costDeleteTree(forest1.get(k));
                    }
                    dp[i][j] = cost;
                } else {
                    double deleteOption = dp[i + 1][j] + costDeleteTree(forest1.get(i));
                    double insertOption = dp[i][j + 1] + costInsertTree(forest2.get(j));
                    double childrenDist = forestDistance(
                            forest1.get(i).getPlans(),
                            forest2.get(j).getPlans()
                    );
                    double replaceOption = childrenDist
                            + costReplace(forest1.get(i), forest2.get(j))
                            + dp[i + 1][j + 1];

                    dp[i][j] = Math.min(deleteOption, Math.min(insertOption, replaceOption));
                }
            }
        }
        return dp[0][0];
    }
}