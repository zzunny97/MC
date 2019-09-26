#include <iostream>
#include <sstream>
#include <pthread.h>
#include <atomic>

#define BILLION  1000000000L
#define NPAIRS  43


using namespace std;

pthread_mutex_t mm1=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mm2=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mm3=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_Level=PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t node_lock=PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond=PTHREAD_COND_INITIALIZER;

pthread_mutex_t mutex[17]=PTHREAD_MUTEX_INITIALIZER;
pthread_rwlock_t rwLock=PTHREAD_RWLOCK_INITIALIZER;

template<class K,class V,int MAXLEVEL>
class skiplist_node
{
	public:
		skiplist_node()
		{
            pthread_mutex_init(&m,NULL);
			for ( int i=1; i<=MAXLEVEL; i++ ) {
				forwards[i] = NULL;
			}
			cnt = 0;
            flag = 0;
		}

		skiplist_node(K searchKey)
		{
            pthread_mutex_init(&m,NULL);
			for ( int i=1; i<=MAXLEVEL; i++ ) {
				forwards[i] = NULL;
			}
			key[0] = searchKey;
			cnt = 1;
            flag = 0;
		}

		skiplist_node(K searchKey,V val)
		{
            pthread_mutex_init(&m,NULL);
			for ( int i=1; i<=MAXLEVEL; i++ ) {
				forwards[i] = NULL;
			}
			key[0] = searchKey;
			value[0] = val;
			cnt = 1;
            flag = 0;
		}

		virtual ~skiplist_node()
		{
		}

		void insert(K k, V v)
		{
            pthread_mutex_lock(&m);
			for(int i=0;i<cnt;i++){
				if( key[i] < k) 
					continue;

				// shift to right
				for(int j=cnt-1;j>=i;j--){
					key[j+1] = key[j] ;
					value[j+1] = value[j] ;
				}
				// insert to the right position
				key[i] = k;
				value[i] = v;
				cnt++;
                pthread_mutex_unlock(&m);
				return;
			}
			key[cnt] = k;
			value[cnt] = v;
			cnt++;
            pthread_mutex_unlock(&m);
			return;
		}
        int getCnt() {
            pthread_mutex_lock(&m);
            int ret = cnt;
            pthread_mutex_unlock(&m);
            return ret;
        }

        void setCnt(int ncnt) {
            pthread_mutex_lock(&m);
            cnt = ncnt; 
            pthread_mutex_unlock(&m);
        }
        int getKey(int i){
            pthread_mutex_lock(&m);
            int ret = key[i];
            pthread_mutex_unlock(&m);
            return ret;
        }
        int getValue(int i) {
            pthread_mutex_lock(&m);
            int ret = value[i];
            pthread_mutex_unlock(&m);
            return ret;
        }

        void printNode() {
            for(int i=0; i<cnt; i++) {
                cout << key[i] << " " << value[i] << endl;
            }
        }


        pthread_mutex_t m;
		int cnt;    //4
        int flag;   //4
		// change KV to array of structure later
		K key[NPAIRS];   // 4*43   --> 172
		V value[NPAIRS];   // 4*43   --> 172
		skiplist_node<K,V,MAXLEVEL>* forwards[MAXLEVEL+1];   // 8*17 = 156 bytes --> 128 + 28 bytes 
        char padding[4];
		// total 344 + 128 + 28 + 4 + 4 + 4-->  512 bytes --> 8 cachelines
};

///////////////////////////////////////////////////////////////////////////////

template<class K, class V, int MAXLEVEL = 16>
class skiplist
{
	public:
		typedef K KeyType;
		typedef V ValueType;
		typedef skiplist_node<K,V,MAXLEVEL> NodeType;

		skiplist(K minKey,K maxKey):m_pHeader(NULL),m_pTail(NULL),
		max_curr_level(1),max_level(MAXLEVEL),
		m_minKey(minKey),m_maxKey(maxKey)
	{
        pthread_mutex_init(&mutex_Level, NULL);
		m_pHeader = new NodeType(m_minKey);
		m_pTail = new NodeType(m_maxKey);
		for ( int i=1; i<=MAXLEVEL; i++ ) {
			m_pHeader->forwards[i] = m_pTail;
            pthread_mutex_init(&mutex[i], NULL);
		}
        for(int i=0; i<36;i++) padding[i] = '0';
	}

		virtual ~skiplist()
		{
			NodeType* currNode = m_pHeader->forwards[1];
			while ( currNode != m_pTail ) {
				NodeType* tempNode = currNode;
				currNode = currNode->forwards[1];
				delete tempNode;
			}
			delete m_pHeader;
			delete m_pTail;
		}

		void insert(K searchKey,V newValue)
		{
			skiplist_node<K,V,MAXLEVEL>* update[MAXLEVEL];
			NodeType* currNode = m_pHeader;
            pthread_mutex_lock(&mm1);
            int tmp = getMaxLevel(); 
            pthread_mutex_unlock(&mm1);
			for(int level=tmp; level >=1; level--) {
                //cout << "level = " << level << endl;
				while ( currNode->forwards[level]->key[0] <= searchKey ) {
					currNode = currNode->forwards[level];   // shift to right
				}
				update[level] = currNode;
			}
    

			if( currNode->getCnt() < NPAIRS){
				currNode->insert(searchKey, newValue);
			}
			else { // split
                pthread_mutex_lock(&mm1);
				int newlevel = randomLevel();
				if ( newlevel > getMaxLevel()) {
					for ( int level = getMaxLevel()+1; level <= newlevel; level++ ) {
						update[level] = m_pHeader;
					}
                    max_curr_level.store(newlevel, std::memory_order_relaxed);

				}
				NodeType* newNode = new NodeType();
				int mid=currNode->getCnt()/2; 
				for (int i=mid; i<currNode->getCnt(); i++){
					newNode->insert(currNode->key[i], currNode->value[i]);
				}
                // setCnt
                currNode->setCnt(mid);
				//currNode->cnt = mid;
				if(newNode->getKey(0) < searchKey){
					newNode->insert(searchKey, newValue);
				}
				else{
					currNode->insert(searchKey, newValue);
				}
				for ( int lv=1; lv<=tmp; lv++ ) {
                    pthread_mutex_lock(&mutex[lv]);
					newNode->forwards[lv] = update[lv]->forwards[lv];
					update[lv]->forwards[lv] = newNode;
                    pthread_mutex_unlock(&mutex[lv]);
				}
                pthread_mutex_unlock(&mm1);
			}

		}

		void erase(K searchKey)
		{
			/*
			   skiplist_node<K,V,MAXLEVEL>* update[MAXLEVEL];
			   NodeType* currNode = m_pHeader;
			   for(int level=max_curr_level; level >=1; level--) {
			   while ( currNode->forwards[level]->key < searchKey ) {
			   currNode = currNode->forwards[level];
			   }
			   update[level] = currNode;
			   }
			   currNode = currNode->forwards[1];
			   if ( currNode->key == searchKey ) {
			   for ( int lv = 1; lv <= max_curr_level; lv++ ) {
			   if ( update[lv]->forwards[lv] != currNode ) {
			   break;
			   }
			   update[lv]->forwards[lv] = currNode->forwards[lv];
			   }
			   delete currNode;
			// update the max level
			while ( max_curr_level > 1 && m_pHeader->forwards[max_curr_level] == NULL ) {
			max_curr_level--;
			}
			}
			 */
		}

		//const NodeType* find(K searchKey)
		V find(K searchKey)
		{
			NodeType* currNode = m_pHeader;
			for(int level=max_curr_level; level >=1; level--) {
                pthread_mutex_lock(&mutex[level]);
				while ( currNode->forwards[level]->key[0] <= searchKey ) {
					currNode = currNode->forwards[level]; // shift to right
				}
                pthread_mutex_unlock(&mutex[level]);
			}
			// currNode = currNode->forwards[1];

			for(int i=0;i<currNode->getCnt();i++){
				if ( currNode->getKey(i) == searchKey ) {
					return currNode->getValue(i);
				}
			}

			//return NULL;
			return -1;
		}
        
        int getMaxLevel(){
            return max_curr_level.load(memory_order_relaxed);
        }

        void setMaxLevel(int maxlevel) {
           max_curr_level.store(maxlevel, memory_order_relaxed); 
        }
        

		bool empty() const
		{
			return ( m_pHeader->forwards[1] == m_pTail );
		}

		std::string printList()
		{
			int p=0;
			std::stringstream sstr;
			NodeType* currNode = m_pHeader; //->forwards[1];
			while ( currNode != m_pTail ) {
				for(int i=0;i<currNode->cnt&&p<=200;i++,p++){
					if(currNode == m_pHeader && i ==0) i++;
					sstr << currNode->key[i] << " ";
				}
				currNode = currNode->forwards[1];
				if(p>200) break;
			}
			return sstr.str();
		}

		const int max_level;

	protected:
		double uniformRandom()
		{
			return rand() / double(RAND_MAX);
		}

		int randomLevel() {
			int level = 1;
			double p = 0.5;
			while ( uniformRandom() < p && level < MAXLEVEL ) {
				level++;
			}
			return level;
		}
		K m_minKey;     //4
		K m_maxKey;     //4
		atomic<int> max_curr_level; //4
		skiplist_node<K,V,MAXLEVEL>* m_pHeader; //8
		skiplist_node<K,V,MAXLEVEL>* m_pTail;   //8
        char padding[36];                       //36

        // 12+16+36 = 64
};

///////////////////////////////////////////////////////////////////////////////

