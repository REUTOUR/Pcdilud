#pragma once

template<typename T> class AutomatedLock
{
	public: 
		AutomatedLock(T& t) : lock(t)
		{
			lock.Lock();
		}

		~AutomatedLock()
		{
			lock.Unlock();
		}

	private:
		T& lock;
};
