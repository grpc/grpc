#ifndef PRESS_H_
#define PRESS_H_

#include <stdint.h>
#include <sys/time.h>

#include <vector>
#include <map>

static const int request_max_num = 20000;
 
class PressTest {
 public:
	PressTest() : request_num_(0) {}
	~PressTest() {}

	void SetCurrentTime() {
		cur_time_ = GetCurrentTime();
	}

	int64_t GetLantency() {
			if (++request_num_ > request_max_num) return -1;
			int lantency = (GetCurrentTime()-cur_time_);
			request_time_vec_.push_back(cur_time_);
			lantency_time_vec_.push_back(lantency);
			return lantency;
	}

	int64_t GetCurrentTime() {
		struct timeval val;
		gettimeofday(&val, NULL);
		return (val.tv_sec*1000000+val.tv_usec);
	}

 private:
	int64_t cur_time_;
	int64_t request_num_;

 public:
	std::vector<int64_t> request_time_vec_;
	std::vector<int64_t> lantency_time_vec_;
};


class PressResult {
 public:
	void CollectResult(PressTest* press_test) {
		int size = press_test->request_time_vec_.size();
		for (int i = 0; i < size; i++) {
			result_map_.insert(std::pair<int64_t, int64_t>
												(press_test->request_time_vec_[i], 
												 press_test->lantency_time_vec_[i]));
		}
	}

	void PrintResult() {
		if (result_map_.empty()) return;
		int press_flag = 0;
		int64_t prev_time = result_map_.begin()->first;
		int64_t restrict_time = prev_time + 1000000L;
		int64_t request_num = 0;
		int64_t lantency_total_time = 0;
		for (auto iter = result_map_.begin(); iter != result_map_.end();) {
			if ((iter->first+iter->second) <= restrict_time) {
				request_num++;
				lantency_total_time += iter->second;
				iter++;
			} else {
				press_flag = 1;
				printf("qps:%lu, 平均响应时间：%f\n", request_num, float(lantency_total_time/request_num)/1000);
				restrict_time = iter->first + 1000000L;
				request_num = 0;
				lantency_total_time = 0;
			}
		}

		if (!press_flag) {
			printf("request is not enough\n");
		}
	}

 private:
	std::map<int64_t, int64_t> result_map_; 
};
 

#endif