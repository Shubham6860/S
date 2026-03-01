/**
 * Google Apps Script web app endpoint for parking logs.
 *
 * Sheet columns:
 * Date | Time | Spot | Duration | Stop Reason
 */
function doGet(e) {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  const sheet = ss.getSheetByName('Sheet1') || ss.getActiveSheet();

  const epoch = Number((e.parameter.epoch || '0').trim());
  const spot = (e.parameter.spot || '').trim();
  const duration = (e.parameter.duration || '0').trim();
  const reason = (e.parameter.reason || '').trim();

  const dateObj = epoch > 0 ? new Date(epoch * 1000) : new Date();

  const timezone = Session.getScriptTimeZone();
  const dateText = Utilities.formatDate(dateObj, timezone, 'yyyy-MM-dd');
  const timeText = Utilities.formatDate(dateObj, timezone, 'HH:mm:ss');

  sheet.appendRow([dateText, timeText, spot, duration, reason]);

  return ContentService
    .createTextOutput(JSON.stringify({ status: 'ok' }))
    .setMimeType(ContentService.MimeType.JSON);
}
