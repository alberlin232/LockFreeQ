#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <barrier>
#include <future>

#define THREADS 8
#define TIME 10000

template <class T>
class Node {
public:
    T value;
    std::atomic<Node<T>*> next;
    Node(T value): value(value), next(nullptr) {}
};

template <class T>
struct alignas(64) PaddedAtomic {
    std::atomic<Node<T>*> value;
};

template <class T>
class LockFreeQ {
public:
    PaddedAtomic<T> head;
    PaddedAtomic<T> tail;

    LockFreeQ() {
        Node<T>* dummy = new Node<T>(T());
        head.value.store(dummy);
        tail.value.store(dummy);
    }

    ~LockFreeQ() {
        while (deq() != -1) {}
        delete head.value.load();
    }

    void enq(T value) {
        Node<T>* node = new Node<T>(value);
        while(true) {
            Node<T>* last = tail.value.load();
            Node<T>* next = last->next.load();
            if (last == tail.value.load()) {
                if (next == nullptr) {
                    if (last->next.compare_exchange_strong(next, node)) {
                        tail.value.compare_exchange_strong(last, node);
                        return;
                    }
                } else {
                    tail.value.compare_exchange_strong(last, next);
                }
            }
        }
    }

    T deq() {
        while(true) {
            Node<T>* first = head.value.load();
            Node<T>* last = tail.value.load();
            Node<T>* next = first->next.load();
            if (first == head.value.load()) {
                if (first == last) {
                    if (next == nullptr) {
                        return -1; // Queue is empty
                    }
                    tail.value.compare_exchange_strong(last, next);
                } else {
                    T value = next->value;
                    if (head.value.compare_exchange_strong(first, next)) {
                        delete first; // Safe to delete the old head
                        return value;
                    }
                }
            }
        }
    }
};

long rand_range_re(unsigned int *seed, long r)
{
    int m = 2147483647;
    long d, v = 0;

    do
    {
        d = (m > r ? r : m);
        v += 1 + (long)(d * ((double)rand_r(seed) / ((double)(m) + 1.0)));
        r -= m;
    } while (r > 0);
    return v;
}
long do_work(LockFreeQ<int>* q, std::barrier<>* sync, std::atomic<bool>& stop) {
    unsigned int seed = std::hash<std::thread::id>()(std::this_thread::get_id());

    sync->arrive_and_wait();
    long iterations = 0;
    while (!stop) {
        if (rand_range_re(&seed, 2) == 1) {
            q->enq((int)rand_range_re(&seed, 10000));
        } else {
            q->deq();
        }
        iterations++;
    }
    return iterations;
}

int main() {
    LockFreeQ<int> queue;
    std::vector<std::thread> threads(THREADS);
    std::vector<std::future<long>> futures(THREADS);
    std::atomic<bool> stop = false;
    std::barrier sync(THREADS+1);

    for (int i = 0; i < THREADS; i++) {
        std::promise<long> promise;
        futures[i] = promise.get_future();
        threads[i] = std::thread([&queue, &sync, &stop, p = std::move(promise)]() mutable {
            long result = do_work(&queue, &sync, stop);
            p.set_value(result);
        });
    }

    sync.arrive_and_wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(TIME));
    stop = true;

    long sum = 0;
    for (int i = 0; i < THREADS; i++) {
        threads[i].join(); // Wait for the thread to finish
        sum += futures[i].get(); // Get the result from the future
    }

    std::cout << "Total iterations: " << sum << std::endl;
    return 0;
}