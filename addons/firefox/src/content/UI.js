var UI = {
  Panel: {
    show: function( parent,
                    default_ref_selection = 'all',
                    is_context_menu = false )
    {
      this._panel = d3.select('#vislink-panel');
      //panel.select('.info').text( JSON.stringify([...concept_graph.selection]) );

      var self = this;
      this._panel.select('.selection')
        .on('DOMAttrModified', function()
        {
          if(    d3.event.target != d3.event.originalTarget
              || d3.event.attrName != 'checked' )
            return;

          self._update();
        });

      var selection_items =
        this._panel
        .select('.selection')
        .selectAll('li')
        .data([...concept_graph.getSelection()].sort(function(lhs, rhs)
        {
          if( lhs.type != rhs.type )
            return lhs.type != 'concept';
          else
            return lhs.id > rhs.id;
        }));

      selection_items.enter()
        .append('li')
        .append('checkbox');
      selection_items.exit()
        .remove();

      selection_items.each(function(d)
      {
        var self = d3.select(this);

        self.select('checkbox')
          .attr('id', function(d) { return 'ref-check-' + d.id; })
          .attr('label', function(d)
          {
            return d.name || (d.source.name + ' <-> ' + d.target.name);
          })
          .attr('checked', true);
        self
          .classed('concept', function(d) { return d.type == 'concept'; })
          .classed('relation', function(d) { return d.type == 'relation'; })
          .style('background-color', function(d) { return d.getColor(); })
          .style('color', function(d) { return contrastColor(d.getColor()); });
      });

      var selection_valid = !content.getSelection().isCollapsed;

      $('ref-scope').selectedItem =
        selection_valid && (default_ref_selection == 'selection')
        ? $('ref-selection')
        : $('ref-all');

      d3.select('#ref-selection')
        .property('disabled', !selection_valid);

      this._update();
      this._panel.node().openPopup(
        parent,
        'bottomcenter topright',
        0, 0,
        is_context_menu,
        false
      );
    },
    getSelection: function()
    {
      var selection = new Set();
      this._panel
        .select('.selection')
        .selectAll('checkbox[checked=true]')
        .each(function(d)
        {
          selection.add(d);
        });
      return selection;
    },

    /** Get selection ids as array (with ids of relations beeing arrays of node
     *  ids)
     */
    getSelectionIdArray: function()
    {
      var ids = [];
      for(var item of this.getSelection())
        if( item.type == 'concept' )
          ids.push(item.id);
        else
          ids.push(item.nodes);
      return ids;
    },
    _onAddReference: function(button)
    {
      utils.addReference(this.getSelectionIdArray(), $('ref-scope').value);
      this._panel.node().hidePopup();
    },
    _onCreateRelation: function(button)
    {
      utils.addRelation(this.getSelectionIdArray(), $('ref-scope').value);
      this._panel.node().hidePopup();
    },
    _onCreateConcept: function(button)
    {
      utils.addConcept($('ref-scope').value);
      this._panel.node().hidePopup();
    },
    _update: function()
    {
      var selection = this.getSelection();
      var concept_count = 0,
          relation_count = 0;

      for(var item of selection)
      {
        if( item.type == 'concept')
          concept_count += 1;
        else
          relation_count += 1;
      }

      $('btn-create-relation').disabled = relation_count || concept_count != 2;
      $('btn-add-reference').disabled = !relation_count && !concept_count;

      var hide_selection_actions = !concept_graph.getSelection().size;
      $('btn-create-relation').hidden = hide_selection_actions;
      $('btn-add-reference').hidden = hide_selection_actions;
      $('sec-selection').hidden = hide_selection_actions;
    }
  },
  showPreferences: function()
  {
    window.open( 'chrome://hidden-content/content/preferences.xul',
                 'VisLinks - Settings',
                 'chrome' );
  }
};