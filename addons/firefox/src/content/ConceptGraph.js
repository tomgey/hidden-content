function ConceptGraph()
{
  /** Event handlers for graph changes */
  this.handlers = new Map();

  /** Concepts (indexed by id) */
  this.concepts = new Map();

  /** Concept relations ( index by colon separated list of alphabetically sorted
   *                      concept ids, eg. 'id-concept-1:id.concept-2' )
   */
  this.relations = new Map();

  /** Selected concept and relation ids */
  this.selection = new Set();
}

/**
 * A concept
 */
function Concept(cfg)
{
  for(var prop in cfg)
    if( this[prop] )
      console.warn('can not override member by property', prop);
    else
      this[prop] = cfg[prop];
}

Concept.prototype.getLabel = function()
{
  return this.name;
}

Concept.prototype.getType = function()
{
  return 'concept';
}

Concept.prototype.isConcept = function() { return true; }
Concept.prototype.isRelation = function() { return false; }

/**
 * A relation between (two) concepts
 */
function Relation(cfg)
{
  for(var prop in cfg)
    if( this[prop] )
      console.warn('can not override member by property', prop);
    else
      this[prop] = cfg[prop];
}

Relation.prototype.getLabel = function()
{
  return this.source.name + ' <-> ' + this.target.name;
}

Relation.prototype.getType = function()
{
  return 'relation';
}

Relation.prototype.isConcept = function() { return false; }
Relation.prototype.isRelation = function() { return true; }

Concept.prototype.getColor =
Relation.prototype.getColor = function()
{
  return this.color || '#eeeeee';
}

/**
 * Register an event handler
 *
 * @param type  Event type
 * @param cb    Callback
 */
ConceptGraph.prototype.on = function(type, cb)
{
  var types = typeof(type) == 'object' ? type : [type];

  types.forEach(function(type)
  {
    if( this.handlers.has(type) )
      console.warn('Replacing event listener for \'' + type + '\'');

    this.handlers.set(type, cb);
  }, this);

  return this;
}

/**
 * Add a new concept
 */
ConceptGraph.prototype.addConcept = function(cfg)
{
  var id = cfg.id;
  if( this.concepts.has(id) )
  {
    console.warn("addConcept: already exists: " + id);
    return false;
  }

  var concept = new Concept(cfg);

  this.concepts.set(id, concept);
  this._callHandler('concept-new', id, concept);

  return true;
}

/**
 * Update concept information
 */
ConceptGraph.prototype.updateConcept = function(new_cfg)
{
  var concept = this.concepts.get(new_cfg.id);
  if( !concept )
  {
    console.warn("Unknown concept: " + new_cfg.id);
    return;
  }

  console.log("Update concept: " + concept.name, new_cfg);
  for(var prop in new_cfg)
    concept[prop] = new_cfg[prop];

  this._callHandler('concept-update', concept.id, concept);
}

/**
 * Remove existing concept
 */
ConceptGraph.prototype.removeConcept = function(id, send_msg = true)
{
  if( !this.concepts.has(id) )
    return false;

  // Remove matching links...
  for(var [rel_id, rel] of this.relations)
  {
    if( rel.nodes.indexOf(id) >= 0 )
      this.removeRelation(rel_id, send_msg);
  }

  this.updateSelection('unset', id, send_msg);
  this.concepts.delete(id);
  if( send_msg )
    send({
      'task': 'CONCEPT-UPDATE',
      'cmd': 'delete',
      'id': id
    });
  this._callHandler('concept-delete', id);

  return true;
}

/**
 * Add a new relation between concepts
 */
ConceptGraph.prototype.addRelation = function(cfg)
{
  var first = this.concepts.get(cfg.nodes[0]),
      second = this.concepts.get(cfg.nodes[1]);

  if( !first)
  {
    console.warn("Unknown concept: " + cfg.nodes[0]);
    return false;
  }
  if( !second )
  {
    console.warn("Unknown concept: " + cfg.nodes[1]);
    return false;
  }

  var id = cfg.nodes.join(':');
  if( this.relations.has(id) )
  {
    console.warn("addRelation: already exists: " + id);
    return false;
  }

  cfg.source = first;
  cfg.target = second;
  cfg.id = id;
  var rel = new Relation(cfg);

  this.relations.set(id, rel);
  this._callHandler('relation-new', id, rel);

  return true;
}

/**
 * Update relation information
 */
ConceptGraph.prototype.updateRelation = function(new_cfg)
{
  var id = new_cfg.nodes.join(':');
  var relation = this.relations.get(id);
  if( !relation )
  {
    console.warn("Unknown relation: [" + new_cfg.nodes + "]");
    return;
  }

  console.log("Update relation: " + relation.id, new_cfg);
  for(var prop in new_cfg)
    relation[prop] = new_cfg[prop];

  this._callHandler('relation-update', relation.id, relation);
}

/**
 * Remove existing relation
 */
ConceptGraph.prototype.removeRelation = function(id, send_event = true)
{
  if( !this.relations.has(id) )
    return false;

  this.updateSelection('unset', id, send_event);
  this.relations.delete(id);
  if( send_event )
    send({
      'task': 'CONCEPT-LINK-UPDATE',
      'cmd': 'delete',
      'id': id
    });
  this._callHandler('relation-delete', id);

  return true;
}

/**
 * Update the selection of concepts or relations
 *
 * @param type ('nodes' or 'links')
 */
ConceptGraph.prototype.updateSelection = function(action, id, send_msg = true)
{
  var changed = false;
  var previous_selection = new Set(this.selection);

  var ids = typeof id == 'object' ? new Set(id) : new Set([id]);

  if( action == 'set' )
  {
    this.selection = ids;
  }
  else if( action == 'toggle' )
  {
    for(var id of ids)
    {
      if( this.selection.has(id) )
        this.selection.delete(id);
      else
        this.selection.add(id);
    }
  }
  else if( action == 'unset' )
  {
    for(var id of ids)
     this.selection.delete(id);
  }
  else
    console.warn('Unknown action: ' + action);

  var self = this;
  var selectionType = function(id)
  {
    return self._isRelationId(id) ? 'relation' : 'concept';
  }

  for(var prev_id of previous_selection)
    if( !this.selection.has(prev_id) )
    {
      this._callHandler('selection-remove', prev_id, selectionType(prev_id));
      changed = true;
    }

  for(var cur_id of this.selection)
    if( !previous_selection.has(cur_id) )
    {
      this._callHandler('selection-add', cur_id, selectionType(cur_id));
      changed = true;
    }

  if( !changed )
    return false;

  this._selected_concept_ids = null;
  this._selected_relation_ids = null;

  this._callHandler('selection-change', this.selection, previous_selection);

  if( send_msg )
    send({
      'task': 'CONCEPT-SELECTION-UPDATE',
      'selection': [...this.selection]
    });

  return true;
}

/**
 * Remove given concept or relation
 */
ConceptGraph.prototype.remove = function(id)
{
  return this._isRelationId(id) ? this.removeRelation(id)
                                : this.removeConcept(id);
}

/**
 * Get the ids of all concepts and relations
 */
ConceptGraph.prototype.getIds = function()
{
  var ids = new Set();
  for(var [id] of this.concepts)
    ids.add(id);
  for(var [id] of this.relations)
    ids.add(id);
  return ids;
}

/**
 * Get the ids of all selected concepts and relations
 */
ConceptGraph.prototype.getSelectionIds = function()
{
  return this.selection;
}

/**
 * Get the selected concepts and relations
 */
ConceptGraph.prototype.getSelection = function()
{
  var selection = new Set();
  this.selection.forEach(function(id)
  {
    if( this._isRelationId(id) )
      selection.add( this.getRelationById(id) );
    else
      selection.add( this.getConceptById(id) );
  }, this);
  return selection;
}

/**
 * Get the id of the selected concept or relation if only a single item is
 * selected.
 */
ConceptGraph.prototype.getSelectedId = function()
{
  if( this.selection.size != 1 )
    return null;

  return this.selection.values().next().value;
}

/**
 * Get the selected concept if only a single concept is selected.
 */
ConceptGraph.prototype.getSelectedConcept = function()
{
  var id = this.getSelectedId();
  if( !id || this._isRelationId(id) )
    return null;

  return this.getConceptById(id);
}

/**
 * Get the selected relation if only a single relation is selected.
 */
ConceptGraph.prototype.getSelectedRelation = function()
{
  var id = this.getSelectedId();
  if( !id || !this._isRelationId(id) )
    return null;

  return this.getRelationById(id);
}

/**
 * Get all selected concepts
 */
ConceptGraph.prototype.getSelectedConceptIds = function()
{
  if( !this._selected_concept_ids )
  {
    this._selected_concept_ids = new Set();
    this.selection.forEach(function(id)
    {
      if( !this._isRelationId(id) )
        this._selected_concept_ids.add(id);
    }, this);
  }

  return this._selected_concept_ids;
}

/**
 * Get all selected concepts
 */
ConceptGraph.prototype.getSelectedConcepts = function()
{
  var concepts = [];
  this.getSelectedConceptIds().forEach(function(id)
  {
    concepts.push( this.getConceptById(id) );
  }, this);

  return concepts;
}

/**
 * Get the ids of all selected relations
 */
ConceptGraph.prototype.getSelectedRelationIds = function()
{
  if( !this._selected_relation_ids )
  {
    this._selected_relation_ids = new Set();
    this.selection.forEach(function(id)
    {
      if( this._isRelationId(id) )
        this._selected_relation_ids.add(id);
    }, this);
  }

  return this._selected_relation_ids;
}

/**
 * Get all selected relations
 */
ConceptGraph.prototype.getSelectedRelations = function()
{
  var relations = [];
  this.getSelectedRelationIds().forEach(function(id)
  {
    relations.push( this.getRelationById(id) );
  }, this);

  return relations;
}

/**
 *
 */
ConceptGraph.prototype.getConceptById = function(id)
{
  return this.concepts.get(id);
}

/**
 *
 */
ConceptGraph.prototype.getRelationById = function(id)
{
  return this.relations.get(id);
}

/**
 * Get relation for the given concepts
 */
ConceptGraph.prototype.getRelationForConcepts = function(id1, id2)
{
  // Relation id is alphabetically ordered, colon separated list of concept ids
  return this.relations.get( id1 < id2 ? id1 + ':' + id2
                                       : id2 + ':' + id1 );
}

ConceptGraph.prototype.handleMessage = function(msg)
{
  console.log('ConceptGraph message:', msg);
  if( msg.task == 'CONCEPT-NEW' )
  {
    delete msg.task;
    this.addConcept(msg);
    return true;
  }
  else if( msg.task == 'CONCEPT-UPDATE' )
  {
    delete msg.task;
    this.updateConcept(msg);
    return true;
  }
  else if( msg.task == 'CONCEPT-DELETE' )
  {
    this.removeConcept(msg.id, false);
    return true;
  }
  else if( msg.task == 'CONCEPT-LINK-NEW' )
  {
    delete msg.task;
    this.addRelation(msg);
    return true;
  }
  else if( msg.task == 'CONCEPT-LINK-UPDATE' )
  {
    delete msg.task;
    this.updateRelation(msg);
    return true;
  }
  else if( msg.task == 'CONCEPT-LINK-DELETE' )
  {
    this.removeRelation(msg.id, false);
    return true;
  }
  else if( msg.task == 'CONCEPT-SELECTION-UPDATE' )
  {
    this.updateSelection('set', msg.selection, false);
    return true;
  }
  else if( msg.task == 'GET-FOUND' && msg.id == '/concepts/all' )
  {
    for(var id in msg.concepts)
      this.addConcept(msg.concepts[id]);

    if( msg.layout )
    {
      for(var id in msg.layout)
      {
        var concept = this.concepts.get(id);
        if( !concept )
          continue;

        for(var k in msg.layout[id])
          this.concepts.get(id)[k] = msg.layout[id][k];

        concept.px = concept.x;
        concept.py = concept.y;
      }
    }

    for(var id in msg.relations)
      this.addRelation(msg.relations[id]);

    this.updateSelection('set', msg.selection, false);
    return true;
  }
  return false;
}

/** Internal callback handler */
ConceptGraph.prototype._callHandler = function(type, ...args)
{
  var cb = this.handlers.get(type);
  if( cb )
    cb(...args, type);
}

/**
 * Check if and id describes a relation (and not a concept)
 */
ConceptGraph.prototype._isRelationId = function(id)
{
  return id && id.indexOf(':') >= 0;
}