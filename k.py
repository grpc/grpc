import requests
from bs4 import BeautifulSoup


def get_quotes(category):
    
    req = requests.post('http://www.powerquotes.net/search_results.asp', data = {'search_word':category})
    soup=BeautifulSoup(req.text,'lxml')
    table=soup.find('table')
    list=table.find_all('table')
    quote_list=[]
    list1=list[2].find_all('p')
    del list1[0]
    if len(list1)<=1:
        return('''Oops! It seems that there are no quotes for that
                category.
                \nYou may consider changing the category ''')
    else:
        print(len(list1))
        for j in range(2):
            del list1[len(list1)-1]
        for i in list1:
            a=i.text
            replace=a.replace("\'",'')
            replace=replace.replace("\xa0\xa0"," ")
            replace=replace.replace("\xa0"," ")
            quote_list.append(replace)
        quote_list=[k for k in quote_list if k!='Read complete Powerquote']  
        sublist=[] 
        t=0
        for h in range(int(len(quote_list)/2)):
        
            sublist.append((quote_list[h+t],quote_list[h+1+t]))
            t=t+1
            

        return sublist      
      

map=get_quotes("motivational")
print(map)


