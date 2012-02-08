
//QUEUE

struct _QueueElement
{
  ushort2 block;
  ushort node;
  ushort _unused_;
  float priority;
  uint state;
};
typedef struct _QueueElement QueueElement;


#define  QueueElementEmpty 0
#define  QueueElementReady 1
#define  QueueElementReading 2
#define  QueueElementRead 0
void checkAndSetQueueState(volatile global QueueElement* element, uint state, uint expected)
{
  while(atomic_cmpxchg(&element->state, expected, state) != expected);
}
void setQueueState(volatile global QueueElement* element, uint state)
{
  uint old = atomic_xchg(&element->state, state);
  //check: old + 1 % (QueueElementReading+1) == state
}

struct _QueueGlobal
{
  uint front;
  uint back;
  int filllevel;
};
typedef struct _QueueGlobal QueueGlobal;

#define QueueLinkActive 0x80000000
uint getQueuePosAndLock(volatile global uint* myQueueLink)
{
  while(true)
  {
    uint old = atomic_or(myQueueLink,QueueLinkActive);
    if(!(old & QueueLinkActive))
    {
      //this one is not active
      return 0;
    }
    else if((old & ~QueueLinkActive) == 0)
    {
      //it is just modified -> load again
      continue;
    }
    else
    {
      //this is the current position
      return (old & ~QueueLinkActive);
    }
  }
}

void setQueuePos(volatile global uint* myQueueLink, uint pos)
{
  uint old = atomic_xchg(myQueueLink, pos | QueueLinkActive);
  //check old == QueueLinkActive
}

void unLockQueuePos(volatile global uint* myQueueLink)
{
  uint old = atomic_xchg(myQueueLink, 0);
}

void Enqueue(volatile global QueueGlobal* queueInfo, 
             volatile global QueueElement* queue,
             volatile global uint* queueLink,
             QueueElement* element,
             uint2 dim,
             uint queueSize)
{
  //check if element is already in tehe Queue:
  volatile global uint* myQueueLink = queueLink + (element->block.x + element->block.y*dim.x + dim.x*dim.y*element->node);
  uint queuePos = getQueuePosAndLock(myQueueLink);
  if(queuePos != 0)
  {
    //just update the priority, could be that this has just been dropped out of the queue.
    //as long as the queue is big enough, this should not influence any other
    //as long as the float is positive this should produce the right result!
    atomic_max((volatile global uint*)(&queue[queuePos].priority), as_uint(element->priority));
  }
  else
  {
    //inc size 
    uint filllevel = atomic_inc(&queueInfo->filllevel);
    
    //make sure there is enough space?
    //assert(filllevel < queueSize)

    //insert a new element
    uint pos = atomic_inc(&queueInfo->back)%queueSize;

    //make sure it is free?
    //checkAndSetQueueState(queue + pos, QueueElementEmpty, QueueElementEmpty);

    //insert the data
    queue[pos].block = element->block;
    queue[pos].priority = element->priority;
    queue[pos].node = element->node;
    write_mem_fence(CLK_GLOBAL_MEM_FENCE);
    
    //set the link
    setQueuePos(myQueueLink, pos);
    write_mem_fence(CLK_GLOBAL_MEM_FENCE);

    //activate the queue element
    setQueueState(queue + pos,  QueueElementReady);
  }
}

bool Dequeue(volatile global QueueGlobal* queueInfo, 
             volatile global QueueElement* queue,
             volatile global uint* queueLink,
             QueueElement* element,
             uint2 dim,
             uint queueSize)
{
  //is there something to get?
  if(atomic_dec(&queueInfo->filllevel) <= 0)
  {
    atomic_inc(&queueInfo->filllevel);
    return false;
  }

  //pop the front of the queue
  uint pos = atomic_inc(&queueInfo->front)%queueSize;
  
  //wait for the element to be written/change to reading
  checkAndSetQueueState(queue + pos, QueueElementReading, QueueElementReady);
  
  //read
  element->block = queue[pos].block;
  element->node =  queue[pos].node;

  volatile global uint* myQueueLink = queueLink + (element->block.x + element->block.y*dim.x + dim.x*dim.y*element->node);

  //remove link
  unLockQueuePos(myQueueLink);

  //free queue element
  setQueueState(queue + pos, QueueElementRead);
  return true;
}
//


float getPenalty(read_only image2d_t costmap, int2 pos)
{
  const sampler_t sampler = CLK_FILTER_NEAREST
                          | CLK_NORMALIZED_COORDS_FALSE
                          | CLK_ADDRESS_CLAMP_TO_EDGE;

  return read_imagef(costmap, sampler, pos).x;
}

//it might be better to do that via opengl or something (arbitrary node shapes etc)
__kernel void prepareRouting(read_only image2d_t costmap,
                           global float* nodes,
                           global uint4* queue,
                           global uint* queue_pos,
                           global uint4* startingPoints,
                           const uint numStartingPoints,
                           const int2 dim,
                           const int2 blocks)
{
  __local bool hasStartingPoint;
  

  int3 id = {get_global_id(0), get_global_id(1), get_global_id(2)};


  if(get_local_id(0) == 0 && get_local_id(1) == 0 && get_local_id(2) == 0)
    hasStartingPoint = false;
  barrier(CLK_LOCAL_MEM_FENCE);

  if(get_local_id(0) < dim.x && get_local_id(01) < dim.y)
  {
    float cost = 0.01f*MAXFLOAT;
    if(startingPoints[id.z].x <= id.x && id.x <= startingPoints[id.z].z &&
        startingPoints[id.z].y <= id.y && id.y <= startingPoints[id.z].w)
    {
        cost = 0;
        hasStartingPoint = true;
    }
    //prepare data
    nodes[dim.x*id.y+id.x + dim.x*dim.y*id.z] = cost;
    if(hasStartingPoint == true)
    {
      //TODO insert into queue
    }
  }
  
  //nodes[dim.x*id.y+id.x] = 255*min(1.0f,getPenalty(costmap, id));
}


local float* accessLocalCost(local float* l_costs, int2 id)
{
  return l_costs + (id.x+1 + (id.y+1)*(get_local_size(0)+2));
}

__kernel void routing(read_only image2d_t costmap,
                      global float* nodes,
                      global uint4* queue,
                      global uint* queue_pos,
                      global uint4* startingPoints,
                      const uint numStartingPoints,
                      const int2 dim,
                      const int2 blocks,
                      local float* l_costs)
{
  int3 id = {get_global_id(0), get_global_id(1), get_global_id(2)};

  //copy cost to local
  int local_linid = get_local_id(1)*get_local_size(0) + get_local_id(0);
  for(int i = local_linid; i < (get_local_size(0)+2)*(get_local_size(1)+2); i += get_local_size(0)*get_local_size(1))
  {
    int2 global_pos = (int2)(get_group_id(0)*get_local_size(0),get_group_id(1)*get_local_size(1));
    global_pos += (int2)(i%(get_local_size(0)+2),i/(get_local_size(0)+2)) - (int2)(1,1);
    
    float incost = 0.01f*MAXFLOAT;
    if(global_pos.x > 0 && global_pos.x < dim.x &&
       global_pos.y > 0 && global_pos.y < dim.y)
      incost = nodes[dim.x*global_pos.y + global_pos.x + dim.x*dim.y*id.z]; 
    l_costs[i] = incost;
  }

  local bool change;
  if(local_linid == 0)
    change = true;
  barrier(CLK_LOCAL_MEM_FENCE);

  int2 localid = (int2)(get_local_id(0),get_local_id(1));
  int2 globalid = (int2)(get_global_id(0),get_global_id(1));

  //iterate over it
  float myPenalty = getPenalty(costmap, globalid);
  float local * myval = accessLocalCost(l_costs, localid);
  while(change)
  {
    if(local_linid == 0)
      change = false;
    barrier(CLK_LOCAL_MEM_FENCE);

    float lastmyval = *myval;
    
    int2 offset;
    for(offset.y = -1; offset.y  <= 1; ++offset.y)
      for(offset.x = -1; offset.x <= 1; ++offset.x)
      {

        float d = native_sqrt((float)(offset.x*offset.x+offset.y*offset.y));
        float penalty = 0.5f*(myPenalty + getPenalty(costmap, globalid + offset));
        float c_other = *accessLocalCost(l_costs, localid + offset);
        *myval = min(*myval, c_other + 0.01f*d + d*penalty);
      }

    if(*myval != lastmyval)
      change = true;    

    barrier(CLK_LOCAL_MEM_FENCE);
  }

  //write back
  nodes[dim.x*globalid.y + globalid.x + dim.x*dim.y*id.z] = *myval;
  //nodes[dim.x*globalid.y + globalid.x + dim.x*dim.y*id.z] =  getPenalty(costmap, globalid);
}



#if 0

/*
 The values stored in the resulting map contain the cost the reach each node and
 which is its predecessor on the path there. It contains 32 Bits for each node
 where the first two bits represent the direction of the predecessor and the
 remaining bits form the actual cost of reaching the node. 
 */

constant uint MASK_VALUE        = 0x3fffffff;
constant uint MASK_DIRECTION    = 0xc0000000;
constant uint VALUE_NOT_VISITED = 0xffffffff;

//------------------------------------------------------------------------------
// Heap
//------------------------------------------------------------------------------
typedef struct
{
  global uint *values; //!< The values to be sorted
  global uint *heap;   //!< Sorted indices (by values)
  uint size;           //!< Current heap size
  int2 dim;            //!< Dimensions of values
} data_t;

#define _value(index) (d->values[ d->heap[index] ] & MASK_VALUE)
#define _parent(index) (index / 2)
#define _child_left(index) (2 * index + 1)
#define _child_right(index) (2 * index + 2)

/**
 * @param index The index of the node to insert
 */
void heap_insert( data_t *d,
                  const uint index )
{
  uint cur = d->size++;
  uint val = _value(cur);
  uint parent = _parent(cur);

  // move up
  while( cur && val < _value(parent) )
  {
    d->heap[ cur ] = d->heap[ parent ];

    cur = parent;
    parent = _parent(cur);
  }
  
  d->heap[ cur ] = index;
}

/**
 *
 * @return Index of the minimum node
 */
uint heap_removeMin( data_t *d )
{
  // Remove first element
  uint cur = 0,
       front = d->heap[ cur ],
       child = _child_left(cur);
  
  // Propagate hole down the tree
  while( child < d->size )
  {
    // Change to right child if it exists and is smaller than the left one
    if( child + 1 < d->size && _value(child + 1) < _value(child) )
      child += 1;

    d->heap[ cur ] = d->heap[ child ];
    cur = child;
    child = _child_left(cur);
  }

  // Put last element into hole...
  uint index = d->heap[ --d->size ];
  uint val = _value(index);
  uint parent = _parent(cur);

  // ...and let it bubble up
  while( cur && val < _value(parent) )
  {
    d->heap[ cur ] = d->heap[ parent ];

    cur = parent;
    parent = _parent(cur);
  }
  
  d->heap[ cur ] = index;
  return front;
}

//------------------------------------------------------------------------------
// Dijkstra
//------------------------------------------------------------------------------
float getPenalty(read_only image2d_t costmap, int x, int y)
{
  const sampler_t sampler = CLK_FILTER_NEAREST
                          | CLK_NORMALIZED_COORDS_FALSE
                          | CLK_ADDRESS_CLAMP_TO_EDGE;

  return read_imagef(costmap, sampler, (int2)(x, y)).x;
}

//------------------------------------------------------------------------------
// Kernel
//------------------------------------------------------------------------------

#define makeIndex(pos, dim) (pos.y * dim.x + pos.x)

__kernel void test_kernel( read_only image2d_t costmap,
                           global uint* nodes,
                           global uint* queue,
                           const int2 dim,
                           const int2 start,
                           const int2 target )
{
  data_t data = {
    .values = nodes,
    .heap = queue,
    .size = 0,
    .dim = dim
  };
  
  const float link_width = 4,
              alpha_L = 0.01,
              alpha_P = 2.99,
              // simplify and scale the cost calculation a bit...
              factor = 100 * (0.5/alpha_L * alpha_P * link_width + 1);


  // Initialize all to "Not visited" and startnode to zero
  for(uint i = 0; i < dim.x * dim.y; ++i)
    nodes[i] = VALUE_NOT_VISITED;

  uint start_index = makeIndex(start, dim);
  nodes[ start_index ] = 0;
  heap_insert(&data, start_index);
  int count = 10;
  uint max_size = 0;
  // Start with Dijkstra    
  do
  {
    max_size = max(data.size, max_size);
    uint index = heap_removeMin(&data);
    int px = index % dim.x, // DON'T use int2 -> don't know why but % doesn't work...
        py = index / dim.x;

    // Update all 8 neighbours
    for( int x = max(px - 1, 0); x <= min(px + 1, dim.x); ++x )
      for( int y = max(py - 1, 0); y <= min(py + 1, dim.y); ++y )
      {
        if( px == x && py == y )
          continue;

        int index_n = makeIndex((int2)(x, y), dim);

        if( nodes[index_n] == VALUE_NOT_VISITED )
          heap_insert(&data, index_n);

        float len = (x == px || y == py) ? 1 : 1.4f;
        float cost = factor
                   * len
                   * (getPenalty(costmap, x, y) + getPenalty(costmap, px, py));

        int total_cost = nodes[index] + (int)cost;
        
        // update/relax
        if( total_cost < nodes[index_n] )
        {
          nodes[index_n] = total_cost;
        }
      }
  } while( data.size && count-- );
  
  nodes[ 0 ] = max_size;
}

#endif