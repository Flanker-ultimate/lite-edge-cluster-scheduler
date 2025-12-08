#include "z3++.h"
#include <spdlog/spdlog.h>
#include <vector>
#include <string>
#include <iomanip>
#include <sstream>
#include "TimeRecorder.h"
using namespace z3;

//Compile command: g++ z3Test.cpp -o test -lz3
/*
四个背包，40个物品，颜色有三种，红白蓝
红色背包只能放红色物品
蓝色背包只能放蓝色物品
白色背包可以放任何颜色物品
白色物品可以放入任何颜色背包
体积和重量都有上限
物品可以被无限分割，比如放1/3到一个背包，2/3到另一个背包
目前的优化目标是尽可能平均分布，即最后四个背包的体积的方差最小
*/
struct Item {
    double volume;
    double weight;
    int price;
    std::string color;
    std::string type;
};

double get_double_value(const expr& e) {
    if (e.is_numeral()) {
        if (e.is_int()) {
            return e.get_numeral_int();
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

int main() {
    context c;
    TimeRecord<chrono::milliseconds> Timer("Z3 Solver");

    // Define constraints for bags
    double max_volume_white = 50.0, max_weight_white = 80.0;
    double max_volume_red = 50.0, max_weight_red = 70.0;
    double max_volume_blue = 50.0, max_weight_blue = 70.0;

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
    double totalV, totalW=0.0;
    for(auto item:items){
        totalV+=item.volume;
        totalW+=item.weight;
    }
    spdlog::info("Total Volume:{} total Weight:{}", totalV, totalW);
    double used_volume_white1 = 0.0, used_weight_white1 = 0.0;
    double used_volume_white2 = 0.0, used_weight_white2 = 0.0;
    double used_volume_red = 0.0, used_weight_red = 0.0;
    double used_volume_blue = 0.0, used_weight_blue = 0.0;
    optimize itemOpt(c);
    

    for (size_t i = 0; i < items.size(); ++i) {
        Item& item = items[i];
        //std::cout << "\nProcessing item " << i << "..." << std::endl;
        Timer.startRecord();

        //optimize itemOpt(c);
        expr white_proportion1 = c.real_const(("white_proportion1_" + std::to_string(i)).c_str());
        expr white_proportion2 = c.real_const(("white_proportion2_" + std::to_string(i)).c_str());
        expr red_proportion = c.real_const(("red_proportion_" + std::to_string(i)).c_str());
        expr blue_proportion = c.real_const(("blue_proportion_" + std::to_string(i)).c_str());
        

        itemOpt.add( white_proportion1 >= c.real_val("0"));
        itemOpt.add( white_proportion2 >= c.real_val("0"));
        itemOpt.add( red_proportion >= c.real_val("0"));
        itemOpt.add( blue_proportion >= c.real_val("0"));
        itemOpt.add(white_proportion1 <= c.real_val("1"));
        itemOpt.add(white_proportion2 <= c.real_val("1"));
        itemOpt.add(red_proportion <= c.real_val("1"));
        itemOpt.add(blue_proportion <= c.real_val("1"));
        expr white1V = c.real_val(double_to_string(used_volume_white1).c_str()) + white_proportion1 * c.real_val(double_to_string(item.volume).c_str());
        expr white2V = c.real_val(double_to_string(used_volume_white2).c_str()) + white_proportion2 * c.real_val(double_to_string(item.volume).c_str());
        expr redV = c.real_val(double_to_string(used_volume_red).c_str()) + red_proportion * c.real_val(double_to_string(item.volume).c_str());
        expr blueV = c.real_val(double_to_string(used_volume_blue).c_str()) + blue_proportion * c.real_val(double_to_string(item.volume).c_str());
        expr white1W = c.real_val(double_to_string(used_weight_white1).c_str()) + white_proportion1 * c.real_val(double_to_string(item.weight).c_str());
        expr white2W = c.real_val(double_to_string(used_weight_white2).c_str()) + white_proportion2 * c.real_val(double_to_string(item.weight).c_str());
        expr redW = c.real_val(double_to_string(used_weight_red).c_str()) + red_proportion * c.real_val(double_to_string(item.weight).c_str());
        expr blueW = c.real_val(double_to_string(used_weight_blue).c_str()) + blue_proportion * c.real_val(double_to_string(item.weight).c_str());
         // Color constraint
        if (item.color == "red") {
            itemOpt.add(blue_proportion == c.real_val("0"));
        } else if (item.color == "blue") {
            itemOpt.add(red_proportion == c.real_val("0"));
        }
        itemOpt.add(white_proportion1+white_proportion2+red_proportion+blue_proportion == c.real_val("1"));
        
        // Volume and weight constraints based on used capacity
        itemOpt.add(white1V <= max_volume_white);
        itemOpt.add(white1W <= max_weight_white);
        
        itemOpt.add(white2V <= max_volume_white);
        itemOpt.add(white2W <= max_weight_white);
       
        itemOpt.add(redV <= max_volume_red);
        itemOpt.add(redW <= max_weight_red);
        
        itemOpt.add(blueV <= max_volume_blue);
        itemOpt.add(blueW <= max_weight_blue);
        
        /*
        // Calculate the mean volume
        expr mean = (white1V+white2V+redV+blueV)/4

        // Objective function: minimize variance
        expr variance = (white1V-mean) * (white1V-mean)+
                         (white2V-mean) * (white2V-mean)+
                         (redV-mean) * (redV-mean)+
                         (blueV-mean) * (blueV-mean);

        // Minimize the variance
        itemOpt.minimize(variance);
        */
        
        itemOpt.minimize(abs(white1V-white2V)+abs(white1V-redV)+abs(white1V-blueV)
                        +abs(redV-blueV)+abs(white2V-redV)+abs(white2V-blueV));
        
        

        // Add objective to maximize remaining allocation in blue or red bags
        // if (item.color == "white") {
        //     itemOpt.maximize(blue_proportion + red_proportion);
        // } else if (item.color == "red") {
        //     itemOpt.maximize(red_proportion);
        // } else if (item.color == "blue") {
        //     itemOpt.maximize(blue_proportion);
        // }

        // Solve for the current item
        if (itemOpt.check() == sat) {
            model m = itemOpt.get_model();
            double w1 = get_double_value(m.eval(white_proportion1));
            double w2 = get_double_value(m.eval(white_proportion2));
            double r = get_double_value(m.eval(red_proportion));
            double b = get_double_value(m.eval(blue_proportion));

            spdlog::info("Item {} Allocation:", i);
            spdlog::info("  White Bag 1 Proportion: {}", w1);
            spdlog::info("  White Bag 2 Proportion: {}", w2);
            spdlog::info("  Red Bag Proportion: {}", r);
            spdlog::info("  Blue Bag Proportion: {}", b);

            // Update used capacity
            used_volume_white1 += w1 * item.volume;
            used_weight_white1 += w1 * item.weight;
            used_volume_white2 += w2 * item.volume;
            used_weight_white2 += w2 * item.weight;
            used_volume_red += r * item.volume;
            used_weight_red += r * item.weight;
            used_volume_blue += b * item.volume;
            used_weight_blue += b * item.weight;
        } else {
            spdlog::warn("No feasible allocation found for item {}.", i);
        }

        Timer.endRecord();
        Timer.print();
        Timer.clearRecord();
    }
    spdlog::info("White Bag1: Volume {} Weight {}", used_volume_white1, used_weight_white1);
    spdlog::info("White Bag2: Volume {} Weight {}", used_volume_white2, used_weight_white2);
    spdlog::info("Blue Bag: Volume {} Weight {}", used_volume_blue, used_weight_blue);
    spdlog::info("Red Bag: Volume {} Weight {}", used_volume_red, used_weight_red);
    return 0;
}
