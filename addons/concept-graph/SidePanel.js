var SidePanel = {
  init: function()
  {
    this._cards = {
      concept: d3.select('.card-concept-details'),
      relation: d3.select('.card-relation-details'),
      selection: d3.select('.card-selection-info')
    };
  },

  showCard: function(card_id, visible = true)
  {
    if( !this._cards )
      this.init();

    return this._cards[card_id]
               .style('display', visible ? 'inline' : 'none');
  },

  /**
   * Dynamic action menu box
   *
   * @param user_actions
   */
  updateActions: function(user_actions)
  {
    var action_box = d3.select('#context-toolbox');
    action_box.selectAll('li')
              .remove();

    for(var a of user_actions)
    {
      if( typeof(a.isEnabled) == 'function' && !a.isEnabled() )
        continue;

      var li = action_box.append('li');
      if( a.icon )
        li.append('span')
          .attr('class', 'material-icons')
          .text(a.icon);
      li.append('span')
        .attr('class', 'action-title')
        .text(a.label);
      li.append('span')
        .attr('class', 'action-shortcut')
        .text(a.shortcuts[0]);
      li.on('click', a.action);
    }
  },

  /**
   *
   */
  showSelectionCard: function(visible = true)
  {
    var card = this.showCard('selection', visible);
    if( !visible )
      return;

    var selected_concepts = concept_graph.getSelectedConcepts(),
        selected_relations = concept_graph.getSelectedRelations();

    var concepts =
      card.select('.concepts')
          .style( 'display', selected_concepts.length ? 'inline' : 'none');

    var li = concepts.select('ul')
                     .selectAll('li')
                     .data(selected_concepts);
    li.enter().append('li');
    li.exit().remove();
    li.text(function(d){ return d.getLabel(); });

    var relations =
      card.select('.relations')
          .style('display', selected_relations.length ? 'inline' : 'none');

    var li = relations.select('ul')
                      .selectAll('li')
                      .data(selected_relations);
    li.enter().append('li');
    li.exit().remove();
    li.text(function(d){ return d.getLabel(); });
  },

  /**
   *
   */
  showConceptDetailsCard: function(concept)
  {
    var card = this.showCard('concept', concept != null);

    if( this._active_concept )
      concept_graph.updateConcept({
        id: this._active_concept.id,
        'user-data': card.select('.user-data').property('value')
      });

    this._active_concept = concept;
    if( !concept )
      return;

    var updateConceptColor = function(new_color)
    {
      var color = new_color || concept.getColor(),
          contrast_color = contrastColor(color);

      card.select('.title-bar')
        .style('background-color', color);
      card.select('.title-bar > *')
        .style('color', contrast_color);
      card.select('.mdl-card__menu')
        .style('color', contrast_color);

      svg.select('.node.selected > ellipse')
       .style('fill', color);
      svg.select('.node.selected > text.id')
      .style('fill', contrast_color);
    };

    updateConceptColor();
    card.select('.mdl-card__title-text').text(concept.name);
    card.select('.concept-image').attr('src', concept.img);

    card.select('#concept-color')
      .property('value', concept.getColor())
      .on('change', function()
      {
        concept_graph.updateConcept({
          id: SidePanel._active_concept.id,
          'color': this.value
        });
      })
      .on('input', function()
      {
        updateConceptColor(this.value);
      });
    var colors =
      card.select('.concept-color')
          .selectAll('a')
          // http://colorbrewer2.org/?type=qualitative&scheme=Dark2&n=8
          .data(['#1b9e77','#d95f02','#7570b3','#e7298a','#66a61e','#e6ab02','#a6761d','#666666'])
          .on('click', function(d)
          {
            updateConceptColor(d);
            concept_graph.updateConcept({
              id: SidePanel._active_concept.id,
              'color': d
            });
          });

    colors.enter()
      .append('a')
      .style('background-color', function(d) { return d; });

    card.select('.user-data').property('value', concept['user-data'] || '');
    card.select('.raw-data').text(JSON.stringify(concept, null, 1));

    this._buildRefList(card, concept.refs, 'concept', concept.id);

    card.select('.concept-edit')
      .on('click', function()
      {
        dlgConceptName.show(function(name){
            concept_graph.updateConcept({
              id: SidePanel._active_concept.id,
              'name': name
            });
          },
          concept.name
        );
      });
  },

  /**
   *
   */
  showRelationDetailsCard: function(relation)
  {
    var card = this.showCard('relation', relation != null);

    if( this._active_relation )
    {
      var active_id = this._active_relation.id;
      this._active_relation = null; // Prevent double updates (eg. in callbacks)

      concept_graph.updateRelation({
        id: active_id,
        'user-data': card.select('.user-data').property('value'),
        'label': card.select('.label').property('value')
      });
    }

    this._active_relation = relation;
    if( !relation )
      return;

    card.select('.concept1').text(relation.source.name);
    card.select('.concept2').text(relation.target.name);
    card.select('.user-data').property('value', relation['user-data'] || '');
    card.select('.label').property('value', relation['label'] || '');
    card.select('.raw-data').text(JSON.stringify(relation, null, 1));

    this._buildRefList(card, relation.refs, 'relation', relation.id);
  },

  // private:
  /**
   *
   */
  _buildRefList: function(card, refs_map, type, id)
  {
    var refs = [];
    for(var url in refs_map)
    {
      var ref = refs_map[url];
      ref['url'] = url;
      refs.push(ref);
    }

//    card.select('.references')
//        .style('display', refs.length ? 'block' : 'none');

    var ul = card.select('.references > ul');
    ul.selectAll('li').remove();
    var li = ul.selectAll('li')
               .data(refs)
               .enter()
               .append('li');

    li.classed('ref-open', function(d) { return active_urls.has(d.url); });
    li.append('a')
      .attr('class', 'ref-img mdl-badge')
      .attr('data-badge', function(d) { return active_urls.get(d.url); })
      .on('click', function(d) { SidePanel._onRefClick(d.url, type, id); })
      .append('img')
        .attr('src', function(d){ return d.icon; });
    li.append('a')
      .attr('class', 'ref-url')
      .attr('target', '_blank')
      .attr('href', function(d){ return d.url; })
      .text(function(d){ return d.title || d.url; })
      .on('click', function(d) { SidePanel._onRefClick(d.url, type, id); });
    li.append('button')
      .attr('class', 'concept-ref-delete mdl-button mdl-button--icon mdl-js-button mdl-js-ripple-effect')
      .on('click', function(d){ concept_graph.removeReference(id, d.url); })
        .append('i')
        .attr('class', 'material-icons')
        .text('delete');
  },
  _onRefClick: function(url, type, id)
  {
    d3.event.preventDefault();

    if( type == 'concept' )
    {
      var concept = concept_graph.getConceptById(id),
          pos = [concept.x, concept.y];
      sendInitiateForNode(concept);
    }
    else
    {
      var rel = concept_graph.getRelationById(id),
          pos = rel.center;
      console.log('rel', rel);
      sendInitiateForNode(rel);
    }

    openURL(url, svgToScreenPos(pos));
  }
};