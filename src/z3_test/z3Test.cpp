#include "c++/z3++.h"
#include <spdlog/spdlog.h>
#include <vector>
#include <string>
#include <iomanip>
#include <sstream>
#include"TimeRecorder.h"
using namespace z3;

//Compile command: g++ z3Test.cpp -o test -lz3

struct Item {
    double volume;
    double weight;
    int price;
    std::string color;
    std::string type;
};
double get_double_value(const expr& e) {
    // 将 Z3 输出的分数表达式转换为浮点数
    if (e.is_numeral()) {
        if (e.is_int()) {
            return e.get_numeral_int();  // 对于整数，直接返回整数值
        } else {
            int num = e.numerator().get_numeral_int();
            int denom = e.denominator().get_numeral_int();
            return static_cast<double>(num) / denom;
        }
    }
    return 0.0;
}

std::string double_to_string(double value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6) << value;
    return oss.str();
}


const std::string RED = "red";
const std::string BLUE = "blue";
const std::string WHITE = "white";

int main() {
    context c;
    optimize opt(c);  // 使用优化上下文
    solver s(c);
    TimeRecord<chrono::milliseconds> Timer("Z3 Solver");
    // List of items
    std::vector<Item> items = {
    {2.5, 1.5, 20, "red", "type1"},
    {1.8, 1.2, 15, "blue", "type2"},
    {3.0, 2.5, 30, "white", "type3"},
    {2.0, 1.5, 25, "red", "type1"},
    {2.2, 1.8, 18, "blue", "type2"},
    {4.0, 3.5, 35, "white", "type4"},
    {3.5, 2.0, 28, "red", "type3"},
    {2.3, 1.7, 22, "blue", "type1"},
    {3.1, 2.6, 27, "white", "type2"},
    {1.9, 1.4, 19, "red", "type4"},
    {3.2, 2.8, 33, "blue", "type3"},
    {4.5, 3.0, 40, "white", "type1"},
    {2.7, 2.1, 24, "red", "type2"},
    {3.8, 2.9, 34, "blue", "type4"},
    {4.1, 3.3, 39, "white", "type3"},
    {2.4, 1.9, 23, "red", "type1"},
    {3.0, 2.2, 29, "blue", "type2"},
    {4.3, 3.6, 42, "white", "type4"},
    {2.9, 2.3, 26, "red", "type3"},
    {3.7, 2.5, 32, "blue", "type1"},
    {4.8, 3.7, 44, "white", "type2"},
    {1.6, 1.1, 16, "red", "type4"},
    {3.4, 2.9, 31, "blue", "type3"},
    {4.0, 3.2, 38, "white", "type1"},
    {2.1, 1.6, 21, "red", "type2"},
    {3.3, 2.7, 30, "blue", "type4"},
    {4.2, 3.4, 41, "white", "type3"},
    {2.6, 2.0, 25, "red", "type1"},
    {3.9, 3.0, 37, "blue", "type2"},
    {4.6, 3.8, 43, "white", "type4"},
    {1.7, 1.2, 17, "red", "type3"},
    {3.6, 2.8, 32, "blue", "type1"},
    {4.4, 3.5, 42, "white", "type2"},
    {2.5, 1.8, 24, "red", "type4"},
    {3.1, 2.3, 28, "blue", "type3"},
    {4.7, 3.6, 45, "white", "type1"},
    {1.8, 1.3, 18, "red", "type2"},
    {3.2, 2.4, 29, "blue", "type4"},
    {4.3, 3.1, 40, "white", "type3"},
    {2.8, 2.1, 27, "red", "type1"}
};
    // 定义背包的体积和重量限制
    double max_volume_white = 60.0;
    double max_weight_white = 80.0;
    double max_volume_red = 50.0;
    double max_weight_red = 70.0;
    double max_volume_blue = 50.0;
    double max_weight_blue = 70.0;

    // 定义物品的分配比例
    std::vector<std::vector<expr>> proportions;
    for (size_t i = 0; i < items.size(); ++i) {
        proportions.push_back({
            c.real_const(("white_proportion1_" + std::to_string(i)).c_str()),
            c.real_const(("white_proportion2_" + std::to_string(i)).c_str()),
            c.real_const(("red_proportion_" + std::to_string(i)).c_str()),
            c.real_const(("blue_proportion_" + std::to_string(i)).c_str())
        });

        // 添加分配比例约束：0 <= proportion <= 1
        for (auto &p : proportions[i]) {
            s.add(p >= c.real_val("0"));
            s.add(p <= c.real_val("1"));
        }

        // 每个物品的比例和为1
        s.add(proportions[i][0] + proportions[i][1] + proportions[i][2] + proportions[i][3] == c.real_val("1"));
    }

    // 添加体积和重量的约束
    expr total_volume_white1 = c.real_val("0");
    expr total_volume_white2 = c.real_val("0");
    expr total_weight_white1 = c.real_val("0");
    expr total_weight_white2 = c.real_val("0");
    expr total_volume_red = c.real_val("0");
    expr total_weight_red = c.real_val("0");
    expr total_volume_blue = c.real_val("0");
    expr total_weight_blue = c.real_val("0");

    for (size_t i = 0; i < items.size(); ++i) {
        const Item& item = items[i];

        // 体积和重量约束
        total_volume_white1 = total_volume_white1 + proportions[i][0] * c.real_val(double_to_string(item.volume).c_str());
        total_weight_white1 = total_weight_white1 + proportions[i][0] * c.real_val(double_to_string(item.weight).c_str());
        total_volume_white2 = total_volume_white2 + proportions[i][1] * c.real_val(double_to_string(item.volume).c_str());
        total_weight_white2 = total_weight_white2 + proportions[i][1] * c.real_val(double_to_string(item.weight).c_str());
        total_volume_red = total_volume_red + proportions[i][2] * c.real_val(double_to_string(item.volume).c_str());
        total_weight_red = total_weight_red + proportions[i][2] * c.real_val(double_to_string(item.weight).c_str());
        total_volume_blue = total_volume_blue + proportions[i][3] * c.real_val(double_to_string(item.volume).c_str());
        total_weight_blue = total_weight_blue + proportions[i][3] * c.real_val(double_to_string(item.weight).c_str());

        // 根据颜色添加放入限制
        if (item.color == "red") {
            s.add(proportions[i][3] == c.real_val("0"));  // 红色物品不能放入蓝色背包
        } else if (item.color == "blue") {
            s.add(proportions[i][2] == c.real_val("0"));  // 蓝色物品不能放入红色背包
        }
    }

    // 背包体积和重量总和约束
    s.add(total_volume_white1 <= c.real_val(double_to_string(max_volume_white).c_str()));
    s.add(total_weight_white1 <= c.real_val(double_to_string(max_weight_white).c_str()));
    s.add(total_volume_white2 <= c.real_val(double_to_string(max_volume_white).c_str()));
    s.add(total_weight_white2 <= c.real_val(double_to_string(max_weight_white).c_str()));
    s.add(total_volume_red <= c.real_val(double_to_string(max_volume_red).c_str()));
    s.add(total_weight_red <= c.real_val(double_to_string(max_weight_red).c_str()));
    s.add(total_volume_blue <= c.real_val(double_to_string(max_volume_blue).c_str()));
    s.add(total_weight_blue <= c.real_val(double_to_string(max_weight_blue).c_str()));

    // 添加均衡约束：最小化各背包之间的体积差和重量差
    opt.add(s.assertions());  // 将已有的约束条件添加到优化问题中

    // 最小化背包之间的体积差异
    opt.minimize(abs(total_volume_white1 - total_volume_white2) +
                 abs(total_volume_white1 - total_volume_red) +
                 abs(total_volume_white1 - total_volume_blue));

    opt.minimize(abs(total_weight_white1 - total_weight_white2) +
                 abs(total_weight_white1 - total_weight_red) +
                 abs(total_weight_white1 - total_weight_blue));
    Timer.startRecord();
    // 求解
    if (opt.check() == sat) {
        model m = opt.get_model();
        spdlog::info("Solution found:");
        for (size_t i = 0; i < items.size(); ++i) {
            spdlog::info("Item {}:", i);
            spdlog::info("  White Bag 1 Proportion: {}", get_double_value(m.eval(proportions[i][0])));
            spdlog::info("  White Bag 2 Proportion: {}", get_double_value(m.eval(proportions[i][1])));
            spdlog::info("  Red Bag Proportion: {}", get_double_value(m.eval(proportions[i][2])));
            spdlog::info("  Blue Bag Proportion: {}", get_double_value(m.eval(proportions[i][3])));
        }
    } else {
        spdlog::warn("No solution found.");
    }
    Timer.endRecord();
    Timer.print();
    return 0;
}
