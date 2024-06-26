/* global describe it cy beforeEach require */

var helper = require('../../common/helper');
var mobileHelper = require('../../common/mobile_helper');
var calcHelper = require('../../common/calc_helper');

describe(['tagmobile', 'tagnextcloud', 'tagproxy'], 'Delete Objects',function() {

	var eventOptions = {
		force: true,
		button: 0,
		pointerType: 'mouse'
	};

	beforeEach(function() {
		helper.setupAndLoadDocument('calc/delete_objects.ods');

		// Click on edit button
		mobileHelper.enableEditingMobile();
	});

	it('Delete Text', function() {
		helper.setDummyClipboardForCopy();
		calcHelper.dblClickOnFirstCell();
		helper.typeIntoDocument('text');
		helper.selectAllText();
		helper.copy();
		helper.expectTextForClipboard('text');
		helper.typeIntoDocument('{del}');
		helper.typeIntoDocument('{ctrl}a');
		helper.textSelectionShouldNotExist();
	});

	it('Delete Shapes', function() {
		mobileHelper.openInsertionWizard();

		// Do insertion
		cy.cGet('body').contains('.menu-entry-with-icon', 'Shape').click();
		cy.cGet('.col.w2ui-icon.basicshapes_rectangle').click();

		// Check that the shape is there
		cy.cGet('.leaflet-pane.leaflet-overlay-pane svg g').should('exist');

		//deletion
		cy.cGet('.bottomright-svg-pane > .leaflet-control-buttons-disabled > .leaflet-interactive')
			.trigger('pointerdown', eventOptions)
			.wait(1000)
			.trigger('pointerup', eventOptions);

		cy.cGet('body').contains('.menu-entry-with-icon', 'Delete').should('be.visible').click();
		cy.cGet('.leaflet-pane.leaflet-overlay-pane svg g').should('not.exist');
	});

	it('Delete Chart' , function() {
		mobileHelper.openInsertionWizard();
		cy.cGet('body').contains('.menu-entry-with-icon', 'Chart').click();
		cy.cGet('.bottomright-svg-pane > .leaflet-control-buttons-disabled > .leaflet-interactive').should('exist');

		//deletion
		cy.cGet('.bottomright-svg-pane > .leaflet-control-buttons-disabled > .leaflet-interactive')
			.trigger('pointerdown', eventOptions)
			.wait(1000)
			.trigger('pointerup', eventOptions);

		cy.cGet('body').contains('.menu-entry-with-icon', 'Delete')
			.should('be.visible').click();

		cy.cGet('.bottomright-svg-pane > .leaflet-control-buttons-disabled > .leaflet-interactive')
			.should('not.exist');
	});
});
