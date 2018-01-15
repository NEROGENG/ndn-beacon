var noble = require('noble');
var Interest = require('/Users/Nero/ndn-js').Interest;
var Name = require('/Users/Nero/ndn-js').Name;
var Blob = require('/Users/Nero/ndn-js').Blob;
var Data = require('/Users/Nero/ndn-js').Data;
var KeyLocatorType = require('/Users/Nero/ndn-js').KeyLocatorType;

var ndnBeaconNameString = "/ndnbeacon/testdevice";
var hmacKeyDigest = [0xF5, 0xA5, 0xFD, 0x42, 0xD1, 0x6A, 0x20, 0x30, 0x27, 0x98, 0xEF, 0x6E, 0xD3, 0x09, 0x97, 0x9B,
                     0x43, 0x00, 0x3D, 0x23, 0x20, 0xD9, 0xF0, 0xE8, 0xEA, 0x98, 0x31, 0xA9, 0x27, 0x59, 0xFB, 0x4B];

// Create an LED interest
var ledInterest = new Interest(
  new Name(ndnBeaconNameString + "/LED/" + String.fromCharCode(100, 250, 20)));
ledInterest.getKeyLocator().setType(KeyLocatorType.KEY_LOCATOR_DIGEST);
ledInterest.getKeyLocator().setKeyData(new Blob(hmacKeyDigest));

// Create a SETURL interest
var setUrlInterest = new Interest(
  new Name(ndnBeaconNameString + "/SETURL"));
setUrlInterest.getName().append("https://www.douyu.com");
setUrlInterest.getKeyLocator().setType(KeyLocatorType.KEY_LOCATOR_DIGEST);
setUrlInterest.getKeyLocator().setKeyData(new Blob(hmacKeyDigest));

// Create a GETURL interest
var getUrlInterest = new Interest(
  new Name(ndnBeaconNameString + "/GETURL"));
getUrlInterest.getKeyLocator().setType(KeyLocatorType.KEY_LOCATOR_DIGEST);
getUrlInterest.getKeyLocator().setKeyData(new Blob(hmacKeyDigest));

// Create a HELP interest
var helpInterest = new Interest(
  new Name(ndnBeaconNameString + "/HELP"));
helpInterest.getKeyLocator().setType(KeyLocatorType.KEY_LOCATOR_DIGEST);
helpInterest.getKeyLocator().setKeyData(new Blob(hmacKeyDigest));

// Start scanning for peripherals with uuid 2220 when Bluetooth is poweredOn.
noble.on('stateChange', function(state) {
  if (state === 'poweredOn') {
    noble.startScanning(['2220'], false);
  } else {
    noble.stopScanning();
  }
});

// Connect to NDN beacon when found
noble.on('discover', function(peripheral) {
  peripheral.connect(function(error) {
    console.log('connected to peripheral: ' + peripheral.uuid);
    peripheral.discoverServices(['2220'], function(error, services) {
      var beaconService = services[0];
      console.log('discovered beacon service');

      // Go through each of NDN beacon's characteristics
      beaconService.discoverCharacteristics([], function(error, characteristics) {
        characteristics.forEach(function(element){
          if (element.uuid === '2221') {
            console.log("This is a read characteristic");
            element.subscribe(function(error) {
              if (error)
                console.log(error);
              else
                console.log('subscribed to read characteristic');
            });
            processReadCharacteristic(element);
          } else if (element.uuid === '2222') {
            console.log("This is a write characteristic");

            // Test HELP
            var helpInterestBuf = helpInterest.wireEncode().buf();
            writeToCharacteristic(helpInterestBuf, element);
            console.log("HELP interest sent. Expect HELP message.");

            // Test LED
            var ledInterestBuf = ledInterest.wireEncode().buf();
            writeToCharacteristic(ledInterestBuf, element);
            console.log("LED interest sent. LED should change color.");

            // Test SETURL
            var setUrlInterestBuf = setUrlInterest.wireEncode().buf();
            writeToCharacteristic(setUrlInterestBuf, element);
            console.log("SETURL interest sent.");

            // Test GETURL
            // var getUrlInterestBuf = getUrlInterest.wireEncode().buf();
            // writeToCharacteristic(getUrlInterestBuf, element);
            // console.log("GETURL interest sent. Expect custom URL.");
          } else 
            console.log("This is an unknown characteristic");
        });
      });
    });
  });
});

function writeToCharacteristic(buffer, characteristic) {
  var pkts = Math.ceil(buffer.length/18);
  for (var i = 0; i < pkts; i++) {
    var bufHeader = Buffer.from(String.fromCharCode(i, pkts));
    var bufContent = buffer.slice(i*18, (i+1)*18);
    characteristic.write(Buffer.concat([bufHeader, bufContent]), true, function(error) {
      if (error)
        console.log(error);});
  }
}

function processReadCharacteristic(characteristic) {
  var recvBuf = new Buffer(256);
  // Register event handler
  characteristic.on('data', function(data, isNotification) {
    console.log("Recived data from remote device");
    var i = data[0];
    var pkts = data[1];
    data.copy(recvBuf, i*18, 2, data.length);
    if (i === pkts - 1) {
      var total = i*18 + data.length - 2;
      console.log("Total length: " + total);
      recvBuf = recvBuf.slice(0, total);

      // Sample usage of received data packet
      var recvData = new Data();
      recvData.wireDecode(new Blob(recvBuf, false));
      console.log("name: " + recvData.getName().toUri());
      if (recvData.getContent().size() > 0) {
        console.log("content (raw): " + recvData.getContent().buf().toString('binary'));
        console.log("content (hex): " + recvData.getContent().toHex());
      }
    }
  });
}