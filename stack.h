

#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif

template <class T> class stack;

template <class T> class selem
{
public:
	selem() {next = 0;}
	~selem() {if(next) delete next;}

protected:
	T elem;
	selem<T> *next;

	friend class stack;
};

template <class T> class stack
{
public:
	stack() {elems = 0;}
	~stack() {if(elems) delete elems;}

	void Push(T& data)
	{
		selem<T> *temp = new selem<T>;
		temp->elem = data;
		temp->next = elems;
		elems = temp;
	}

	T Pop()
	{
		if(elems)
		{
			T retval = elems->elem;
			selem<T> *temp = elems;
			elems = elems->next;
			temp->next = 0;
			delete temp;
			return retval;
		}

		return 0;
	}

	T Peek()
	{
		if(elems)
            return elems->elem;
		return 0;
	}

	unsigned int Count()
	{
		selem<T> *e = elems;
		unsigned int count = 0;
		while(e != 0)
		{
			++count;
			e = e->next;
		}
		return count;
	}

	bool IsEmpty()
	{
		return elems == 0;
	}
protected:
	selem<T> *elems;
};


typedef stack<WCHAR *> stackString;

