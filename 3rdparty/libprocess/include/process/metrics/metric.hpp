#ifndef __PROCESS_METRICS_METRIC_HPP__
#define __PROCESS_METRICS_METRIC_HPP__

#include <atomic>
#include <memory>
#include <string>

#include <process/future.hpp>
#include <process/owned.hpp>
#include <process/statistics.hpp>
#include <process/timeseries.hpp>

#include <stout/duration.hpp>
#include <stout/option.hpp>
#include <stout/synchronized.hpp>

namespace process {
namespace metrics {

// The base class for Metrics such as Counter and Gauge.
class Metric {
public:
  virtual ~Metric() {}

  virtual Future<double> value() const = 0;

  const std::string& name() const
  {
    return data->name;
  }

  Option<Statistics<double>> statistics() const
  {
    Option<Statistics<double>> statistics = None();

    if (data->history.isSome()) {
      synchronized (data->lock) {
        statistics = Statistics<double>::from(*data->history.get());
      }
    }

    return statistics;
  }

protected:
  // Only derived classes can construct.
  Metric(const std::string& name, const Option<Duration>& window)
    : data(new Data(name, window)) {}

  // Inserts 'value' into the history for this metric.
  void push(double value) {
    if (data->history.isSome()) {
      Time now = Clock::now();

      synchronized (data->lock) {
        data->history.get()->set(value, now);
      }
    }
  }

private:
  struct Data {
    Data(const std::string& _name, const Option<Duration>& window)
      : name(_name),
      // TODO(aclemmer): taken out because of copy constructor madness! Fix this!
        /*lock(ATOMIC_FLAG_INIT),*/
        history(None())
    {
      if (window.isSome()) {
        history =
          Owned<TimeSeries<double>>(new TimeSeries<double>(window.get()));
      }
    }

    const std::string name;

    std::atomic_flag lock;

    Option<Owned<TimeSeries<double>>> history;
  };

  std::shared_ptr<Data> data;
};

} // namespace metrics {
} // namespace process {

#endif // __PROCESS_METRICS_METRIC_HPP__
