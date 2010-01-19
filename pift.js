// $Id$

/**
 * Konami code implementation.
 *
 * Spawn loads of Druplicons.
 *
 * @link http://en.wikipedia.org/wiki/List_of_Konami_code_websites
 */
Drupal.behaviors.piftKonami = function() {

  var kkeys = [];
  var konami = "38,38,40,40,37,39,37,39,66,65";

  $(window).keydown(function(event) {
    kkeys.push(event.keyCode);
    if (kkeys.toString().indexOf(konami) >= 0) {
      // Subtract Druplicon width and height to ensure that he is only spawned
      // inside the window area and does not cause it to scroll.
      var width = $(window).width() - 175;
      var height = $(window).height() - 200;
      var druplicon = 'http://drupalcode.org/viewvc/drupal/contributions/docs/marketing/logo/druplicon.small.png?view=co';
      var max = 500;
      var count = 0;
      
      spawnDruplicon(width, height, druplicon, max, count);
    }
  });
}

/**
 * Spawn a Druplicon at a random location on the screen.
 */
function spawnDruplicon(width, height, druplicon, max, count) {
  // Generate random location.
  var x = Math.floor(Math.random() * width);
  var y = Math.floor(Math.random() * height);

  // Append Druplicon image tag to HTML body.
  $('body').append('<img src="' + druplicon + '" style="position: absolute; z-index: 1000; left: ' + x + 'px; top: ' + y + 'px;"/>');
  count++;
  
  // Queue another Druplicon.
  if (count < max) {
    setTimeout('spawnDruplicon(' + width + ', ' + height + ', "' + druplicon + '", ' + max + ', ' + count + ')', 10);
  }
}
