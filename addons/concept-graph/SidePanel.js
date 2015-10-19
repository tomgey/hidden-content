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

    var concepts =
      card.select('.concepts')
          .style( 'display', selection.nodes.size ? 'inline' : 'none');

    var li = concepts.select('ul')
                     .selectAll('li')
                     .data([...selection.nodes]);
    li.enter().append('li');
    li.exit().remove();
    li.text(function(d){ return d; });

    var relations =
      card.select('.relations')
          .style('display', selection.links.size ? 'inline' : 'none');

    var li = relations.select('ul')
                      .selectAll('li')
                      .data([...selection.links]);
    li.enter().append('li');
    li.exit().remove();
    li.text(function(d){ return d; });
  },

  /**
   * 
   */
  showConceptDetailsCard: function(concept_id)
  {
    var active_node = getNodeById(concept_id);
    var card = this.showCard('concept', active_node != null);

    if( this._active_concept )
    {
      var user_data = card.select('.user-data').property('value');
      updateConcept(this._active_concept, 'user-data', user_data);
    }
    this._active_concept = active_node ? active_node.id : null;

    if( !active_node )
      return;

    var updateConceptColor = function(new_color)
    {
      var color = new_color || nodeColor(active_node),
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
    card.select('.mdl-card__title-text').text(active_node.name);
    card.select('.concept-image').attr('src', active_node.img);

    card.select('#concept-color')
      .property('value', nodeColor(active_node))
      .on('change', function()
      {
        updateConcept(SidePanel._active_concept, 'color', this.value)
      })
      .on('input', function()
      {
        updateConceptColor(this.value);
      });

    card.select('.user-data').property('value', active_node['user-data'] || '');
    card.select('.raw-data').text(JSON.stringify(active_node, null, 1));

    this._buildRefList(card, active_node.refs, 'concept', active_node.id);

    card.select('.concept-edit')
      .on('click', function()
      {
        dlgConceptName.show(function(name){
            updateConcept(SidePanel._active_concept, 'name', name);
          },
          active_node.name
        );
      });
  },

  /**
   * 
   */
  showRelationDetailsCard: function(link_id)
  {
    var active_link = getLinkById(link_id);
    var card = this.showCard('relation', active_link != null);

    if( this._active_link )
    {
      var user_data = card.select('.user-data').property('value');
      updateRelation(this._active_link, 'user-data', user_data);

      var label = card.select('.label').property('value');
      updateRelation(this._active_link, 'label', label);
    }
    this._active_link = active_link ? active_link.id : null;

    if( !active_link )
      return;

    card.select('.concept1').text(active_link.source.name);
    card.select('.concept2').text(active_link.target.name);
    card.select('.user-data').property('value', active_link['user-data'] || '');
    card.select('.label').property('value', active_link['label'] || '');
    card.select('.raw-data').text(JSON.stringify(active_link, null, 1));

    this._buildRefList(card, active_link.refs, 'relation', active_link.id);
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

    card.select('.references')
        .style('display', refs.length ? 'block' : 'none');

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
      .append('img')
        .attr('src', function(d){ return d.icon; });
    li.append('a')
      .attr('class', 'ref-url')
      .attr('href', function(d){ return d.url; })
      .text(function(d){ return d.title || d.url; });
    li.append('button')
      .attr('class', 'concept-ref-delete mdl-button mdl-button--icon mdl-js-button mdl-js-ripple-effect')
      .on('click', function(d){
        send({
          'task': type == 'concept' ? 'CONCEPT-UPDATE-REFS'
                                    : 'CONCEPT-LINK-UPDATE-REFS',
          'cmd': 'delete',
          'url': d.url,
          'id': id
        });
      })
        .append('i')
        .attr('class', 'material-icons')
        .text('delete');
  }
};