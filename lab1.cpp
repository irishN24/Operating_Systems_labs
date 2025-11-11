#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

using namespace std;

class Event {
private:
	mutex mutx; // Мьютекс для синхронизации
	condition_variable cv; // Переменная для ожидания событий
	bool ready = false; // Флаг состояния события
public:
	void producer(); // Метод производителя
	void consumer(); // Метод потребителя
};
void Event::producer() {
	while (true) {
		{
			unique_lock<mutex> lock(mutx);
			if (ready) {
				continue;
			}
			ready = true;
			cout << "Producer: The event is send!\n";
			cv.notify_one();
		}		
		this_thread::sleep_for(chrono::seconds(1));
	}	
}
void Event::consumer() {
	while (true) {
		unique_lock<mutex> lock(mutx);
		cv.wait(lock, [this] { return ready; });
		ready = false;
		cout << "Consumer: The event has been processed\n";
	}
}
void producer_wrapper(Event* processor) {
	processor->producer();
}
void consumer_wrapper(Event* processor) {
	processor->consumer();
}
int main() {
	Event processor;
	thread producer_thread(producer_wrapper, &processor);
	thread consumer_thread(consumer_wrapper, &processor);
	producer_thread.join();
	consumer_thread.join();
	return 0;
}


