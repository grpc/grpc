#include <iostream>
using namespace std;

struct node 
{
    int num;                
    node *nextptr;             
}*stnode; //node constructed

void make(int n);                 
void printMiddle(struct node *stnode);	            
void print();

 
int main() //Main method
{
    int n,num;
		
    cout<<"Enter the number of nodes: ";
    cin>>n;
    make(n);
    cout<<"\nLinked list data: \n";		
    print();
    printMiddle(stnode);
   
    return 0;
}
void make(int n) //function to create linked list.
{
    struct node *frntNode, *tmp;
    int num, i;
 
    stnode = (struct node *)malloc(sizeof(struct node));
    if(stnode == NULL)        
    {
        cout<<"Memory can not be allocated";
    }
    else
    {
                                  
        cout<<"Enter the data for node 1: ";
        cin>>num;
        stnode-> num = num;      
        stnode-> nextptr = NULL; //Links the address field to NULL
        tmp = stnode;
 
        for(i=2; i<=n; i++)
        {
            frntNode = (struct node *)malloc(sizeof(struct node)); 
 

            if(frntNode == NULL) "//If frntnode is null no memory cannot be allotted
            {
                cout<<"Memory can not be allocated";
                break;
            }
            else
            {
                cout<<"Enter the data for node "<<i<<": "; // Entering data in nodes.
                cin>>num;
                frntNode->num = num;         
                frntNode->nextptr = NULL;    
                tmp->nextptr = frntNode;     
                tmp = tmp->nextptr;
            }
        }
    }
} 
void print() // method to display linked list
{
    struct node *tmp;
    if(stnode == NULL)
    {
        cout<<"List is empty";
    }
    else
    {
        tmp = stnode;
        cout<<"Linked List\t";
        while(tmp != NULL)
        {
            cout<<tmp->num<<"\t";   
            tmp = tmp->nextptr;                
        }
    }
}
void printMiddle(struct node *stnode)//function to print middle node of the list.
{
    struct node *single_ptr = stnode;
    struct node *twice_ptr = stnode;
 
    if (stnode!=NULL)
    {
        while (twice_ptr != NULL && twice_ptr->nextptr != NULL)
        {
            twice_ptr = twice_ptr->nextptr->nextptr; //moving with twice speed.
            single_ptr = single_ptr->nextptr;
        }
        cout<<"The middle element is "<<single_ptr->num;
    }
}
