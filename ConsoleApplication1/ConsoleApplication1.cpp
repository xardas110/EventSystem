#include <iostream>
#include <functional>
#include <list>
#include <vector>
#include <mutex>
#include <future>


template<typename ... Args>
class EventHandler
{
public:
	typedef std::function<void(Args...)> FunctionHandler;
	typedef unsigned int HandlerId;

	FunctionHandler functionHandler;

	explicit EventHandler(FunctionHandler functionHandler)
		:functionHandler(std::move(functionHandler))
	{
		handlerId = ++handlerIdCounter;
	}

	EventHandler(const EventHandler& other)
	: functionHandler(other.functionHandler), handlerId(other.handlerId)
	{}

	EventHandler(EventHandler&& other) noexcept
	: functionHandler(std::move(other.functionHandler)), handlerId(other.handlerId)
	{}

	EventHandler& operator=(const EventHandler& other)
	{
		handlerId = other.handlerId;
		functionHandler = other.functionHandler;
		return *this;
	}

	EventHandler& operator=(EventHandler&& other) noexcept
	{
		std::swap(functionHandler, other.functionHandler);
		handlerId = other.handlerId;
		return *this;
	}

	bool operator==(const EventHandler& other) const
	{
		return handlerId == other.handlerId;
	}

	void operator()(Args... params) const
	{
		if (functionHandler)
		{
			functionHandler(params...);
		}
	}

	HandlerId Id() const { return handlerId; }

	~EventHandler() = default;

private:
	HandlerId handlerId;
	static std::atomic_uint handlerIdCounter; //std::atomic_uint for thread safety
};

template <typename... Args>
std::atomic_uint EventHandler<Args...>::handlerIdCounter(0);

template<typename... Args>
class Event
{
public:
	typedef EventHandler<Args...> HandlerType;

	Event() = default;
	
	Event(const Event& other)
	{
		std::lock_guard<std::mutex> lock(m_handlersLocker);
		handlers = other.handlers;
	}

	Event(Event&& other) noexcept
	{
		std::lock_guard<std::mutex> lock(m_handlersLocker);
		handlers = std::move(other.handlers);
	}

	Event& operator=(const Event& other)
	{
		std::lock_guard<std::mutex> lock(m_handlersLocker);
		std::lock_guard<std::mutex> lock2(other.m_handlersLocker);
		handlers = other.handlers;
		return *this;
	}

	Event& operator=(Event&& other) noexcept
	{
		std::lock_guard<std::mutex> lock(m_handlersLocker);
		std::lock_guard<std::mutex> lock2(other.m_handlersLocker);
		
		std::swap(handlers, other.handlers);
		
		return *this;
	}
	
	typename HandlerType::HandlerId Add(const HandlerType& handler)
	{
		std::lock_guard<std::mutex> lock(m_handlersLocker);

		handlers.push_back(handler);
		return handler.Id();
	}

	inline typename HandlerType::HandlerId Add(const typename HandlerType::FunctionHandler& handler)
	{
		return Add(HandlerType(handler));
	}

	bool Remove(const HandlerType& handler)
	{
		std::lock_guard<std::mutex> lock(m_handlersLocker);
		
		auto it = std::find(handlers.begin(), handlers.end(), handler);
		if (it != handlers.end())
		{
			handlers.erase(it);
			return true;
		}
		
		return false;	
	}

	bool RemoveId(const typename HandlerType::HandlerId& handlerId)
	{
		std::lock_guard<std::mutex> lock(m_handlersLocker);

		auto it = std::find_if(handlers.begin(), handlers.end(), [handlerId](const HandlerType& handler) {return handler.Id() == handlerId; });
		if (it != handlers.end())
		{
			handlers.erase(it);
			return true;
		}
		return false;
	}

	void Call(Args... params) const
	{
		HandlerCollection handlerCollectionCopy = GetHandlersCopy();

		CallImpl(handlerCollectionCopy, params...);
	}

	inline void operator()(Args... params) const
	{
		Call(params...);
	}

	inline typename HandlerType::HandlerId operator+=(const HandlerType& handler)
	{
		return Add(handler);
	}

	inline bool operator-=(const HandlerType& handler)
	{
		return Remove(handler);
	}

	std::future<void> CallAsync(Args... params) const
	{
		return std::async(std::launch::async, [this](Args... asyncParams)
			{
				call(asyncParams...);
			}, params...);
	}
	
	mutable std::mutex m_handlersLocker;

protected:
	typedef std::list<HandlerType> HandlerCollection;

	void CallImpl(const HandlerCollection& handlers, Args... params) const
	{
		for (const auto& handler : handlers)
		{
			handler(params...);
		}
	}
	
	HandlerCollection GetHandlersCopy() const
	{
		std::lock_guard<std::mutex> lock(m_handlersLocker);
		return handlers;
	}
private:
	
	HandlerCollection handlers;
};

struct TestEvent
{
	Event<int> event;

	void Print()
	{
		std::cout << "TestEvent is printing" << std::endl;
		event(2314);
	}
};

struct Test
{
	int val = 66;
	void CallMeIfImAlive(int num)
	{
		std::cout << "im alive this worked " << num << " " << val << std::endl;
	}
};


void CallMeWhenYouPrint(int num)
{
	std::cout << "I was called when it prints " << num <<std::endl;
}

int main() {
	TestEvent e;
	Test* test = new Test;

	EventHandler<int> event_handler([test](int num) {test->CallMeIfImAlive(num); });
	
	e.event += event_handler;
	e.Print();
	e.event -= event_handler;

	
	delete test;
}
