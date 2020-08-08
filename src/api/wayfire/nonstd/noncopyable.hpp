#ifndef NONCOPYABLE_HPP
#define NONCOPYABLE_HPP

class noncopyable_t
{
  public:
    noncopyable_t()  = default;
    ~noncopyable_t() = default;

  private:
    noncopyable_t(const noncopyable_t&) = delete;
    noncopyable_t& operator =(const noncopyable_t&) = delete;
};

#endif /* end of include guard: NONCOPYABLE_HPP */
