#ifndef __BUFFERS_BUFFER_IMPLEMENTATION_BASE_HPP__
#define __BUFFERS_BUFFER_IMPLEMENTATION_BASE_HPP__


namespace rclcpp
{
namespace intra_process_buffer
{

template<typename BufferT>
class BufferImplementationBase
{

public:

  virtual void dequeue(BufferT& request) = 0;
  virtual void enqueue(BufferT request) = 0;

  virtual void clear() = 0;
  virtual bool hasData() const = 0;

};

}
}

#endif // __BUFFERS_BUFFER_IMPLEMENTATION_BASE_HPP__
