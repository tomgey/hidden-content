function contrastColor(color)
{
  if( color.length != 7 || color[0] != '#' )
    return '#000000';

  var gamma = 2.2;
  var L = 0.2126 * Math.pow(parseInt(color.substr(1, 2), 16) / 255, gamma)
        + 0.7152 * Math.pow(parseInt(color.substr(3, 2), 16) / 255, gamma)
        + 0.0722 * Math.pow(parseInt(color.substr(5, 2), 16) / 255, gamma);

  return L > 0.5 ? '#000000' : '#ffffff';
}