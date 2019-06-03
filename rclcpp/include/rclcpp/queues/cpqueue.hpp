#ifndef __CONSUMERPRODUCERQUEUE_HPP__
#define __CONSUMERPRODUCERQUEUE_HPP__

#include <mutex>
#include <vector>


template<typename T>
struct ConsumerProducerQueue
{

  ConsumerProducerQueue(size_t size)
  {
    if (size < 0) {
      throw std::invalid_argument("size must be a positive or zero value");
    }

    max_size_ = size;
    unbounded_max_size_ = size;
    head_ = 0;
    tail_ = 0;
    next_push_overwrite_ = false;
    has_data_ = false;

    if (max_size_ > 0){
      buffer_.resize(max_size_);
      unbounded_ = false;
    }
    else{
      unbounded_ = true;
    }
  }

  void
  add(T element)
  {
    std::lock_guard<std::mutex> lock(data_mutex_);

    if (unbounded_){
      if (tail_ >= unbounded_max_size_){
        buffer_.push_back(element);
        unbounded_max_size_ ++;
        tail_ ++;
      }
      else {
        buffer_[tail_] = element;
        tail_++;
      }
      has_data_ = true;
    }
    else{
      buffer_[tail_] = element;

      tail_ = (tail_ + 1) % max_size_;
      if (next_push_overwrite_){
        head_ = (head_ + 1) % max_size_;
      }
      next_push_overwrite_ = (tail_ == head_);
      has_data_ = true;
    }
  }

  void
  consume(T& element)
  {
    std::lock_guard<std::mutex> lock(data_mutex_);

    element = std::move(buffer_[head_]);

    if (unbounded_){
      head_++;
      has_data_ = (head_ != tail_);
      if (!has_data_){
        head_ = 0;
        tail_ = 0;
      }
    }
    else{
      next_push_overwrite_ = false;
      head_ = (head_ + 1) % max_size_;
      has_data_ = (head_ != tail_);
    }
  }

  bool
  has_data()
  {
    return has_data_;
  }

  std::vector<T> buffer_;
  // where I pop
  size_t head_;
  // where I push
  size_t tail_;
  // maximum number of elements in a bounded buffer
  size_t max_size_;
  // is the buffer unbounded?
  bool unbounded_;
  //the number of elements already initialized in the buffer
  size_t unbounded_max_size_;
  // will the next push overwrite some data?
  // if yes, then I have to move head to the new oldest data in queue
  bool next_push_overwrite_;

  bool has_data_;
  std::mutex data_mutex_;
};

#endif