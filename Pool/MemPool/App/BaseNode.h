/// 
///  file  BaseNode.h
///  brief һ��˫��������Ҫ��ǰ��ָ��ڵ�
/// 

#pragma  once 

template < typename  T >
class  CNode
{
	typedef  T  Node;

public:

	Node*	next;
	Node*   prev;

	CNode()
	{
		next = prev = NULL;
	}
};