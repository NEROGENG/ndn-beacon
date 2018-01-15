/*
 Copyright (c) 2014 OpenSourceRF.com.  All right reserved.

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 See the GNU Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

/*
 * 
 * quanjiegeng@cs.ucla.edu
 */

#include <string>
#include <RFduinoBLE.h>
#include <config.h>
#include <ndn-cpp/lite/data-lite.hpp>
#include <ndn-cpp/lite/interest-lite.hpp>
#include <ndn-cpp/lite/encoding/tlv-0_2-wire-format-lite.hpp>
#include <ndn-cpp/lite/util/blob-lite.hpp>
#include <ndn-cpp/lite/util/crypto-lite.hpp>

using namespace ndn;

static ndn_Error replyToInterest(const uint8_t *element, size_t elementLength);

static void processLedInterest(const std::string& data);
static void processSetUrlInterest(const std::string& data);
static ndn_Error processGetUrlInterest();
static ndn_Error processHelpInterest();
static ndn_Error generateResponse(const std::string& msg, const std::string& method);
void sendData(char *data, int len);

std::string readString(const BlobLite& blob) {
  return std::string(reinterpret_cast<const char*>(blob.buf()), blob.size());
}

std::string myUrl = "https://www.douyu.com";

int r_led = 2;
int g_led = 3;
int b_led = 4;

uint8_t hmacKey[64];
uint8_t hmacKeyDigest[ndn_SHA256_DIGEST_SIZE];

uint8_t recv_buf[256];
uint8_t encoding[256];
size_t encodingLength;

int send_flag = false;

void setup() {
  Serial.begin(9600);
  while (!Serial); 

  // Set hmacKey to a default test value.
  memset(hmacKey, 0, sizeof(hmacKey));
  // Set hmacKeyDigest to sha256(hmacKey).
  CryptoLite::digestSha256(hmacKey, sizeof(hmacKey), hmacKeyDigest);
  
  pinMode(r_led, OUTPUT);
  pinMode(g_led, OUTPUT);  
  pinMode(b_led, OUTPUT);
  
  //start announcing
  RFduinoBLE.advertisementData = "RFduino #1";
  RFduinoBLE.deviceName = "testdevice";
  
  // start the BLE stack
  RFduinoBLE.begin();
}


void loop() {
  if (send_flag) {
    sendData((char *)encoding, encodingLength);
    send_flag = false;
  }
}

void RFduinoBLE_onConnect()
{
  // ready to connect again when done
  Serial.println("Device connected");
  digitalWrite(r_led, HIGH);
  digitalWrite(g_led, HIGH);
  digitalWrite(b_led, HIGH);
}


void RFduinoBLE_onDisconnect()
{
  Serial.println("Device disconnected");
  // ready to connect again when done
  digitalWrite(r_led, LOW);
  digitalWrite(g_led, LOW);
  digitalWrite(b_led, LOW);
}

void RFduinoBLE_onReceive(char *data, int len)
{
  Serial.println("Recived data from remote device");
  int i = data[0];
  int pkts = data[1];
  memcpy(recv_buf+i*18, data+2, len-2);
  if (i == pkts - 1) {
    int total = i*18 + len - 2;
    Serial.print("Total length: ");
    Serial.println(total);
    replyToInterest((const uint8_t *)recv_buf, total);
  }
}

void sendData(char *data, int len) {
  Serial.println("Start sending...");
  char sendBuf[20];
  char i;
  char pkts;
  if (len % 18 != 0)
    pkts = (char)(len/18 + 1); 
  else
    pkts = (char)(len/18 + 1);
  for (i = 0; i < pkts - 1; i++) {
    sendBuf[0] = i;
    sendBuf[1] = pkts;
    memcpy(sendBuf+2, data+i*18, 18);
    while (!RFduinoBLE.send(sendBuf, 20));
  }
  sendBuf[0] = i;
  sendBuf[1] = pkts;
  memcpy(sendBuf+2, data+i*18, len%18);
  while (!RFduinoBLE.send(sendBuf, len%18+2));
  Serial.print("Data sent! Total length is: ");
  Serial.println(len);
}

/** 
 * Decode the element as an interest and check the prefix. 
 */
static ndn_Error replyToInterest(const uint8_t *element, size_t elementLength)
{
  // Decode the element as an InterestLite.
  ndn_NameComponent interestNameComponents[4];
  struct ndn_ExcludeEntry excludeEntries[2];
  InterestLite interest
    (interestNameComponents, sizeof(interestNameComponents) / sizeof(interestNameComponents[0]), 
     excludeEntries, sizeof(excludeEntries) / sizeof(excludeEntries[0]), 0, 0);
  size_t signedPortionBeginOffset, signedPortionEndOffset;
  ndn_Error error;
  if ((error = Tlv0_2WireFormatLite::decodeInterest
       (interest, element, elementLength, &signedPortionBeginOffset, 
        &signedPortionEndOffset))) {
          Serial.print("Decoding Error: ");
          Serial.println(error);
          return error;
  }

  // We expect the interest name to be "/ndnbeacon/<device name>/<method type>/<additional param>". 
  // Check the size and prefix /ndnbeacon/<device name> here. 
  if (interest.getName().size() < 3 || interest.getName().size() > 4
  || readString(interest.getName().get(0).getValue()) != "ndnbeacon"
  || readString(interest.getName().get(1).getValue()) != RFduinoBLE.deviceName) {
    // Ignore an unexpected prefix.
    Serial.println("unexpected prefix");
    Serial.println(readString(interest.getName().get(0).getValue()).c_str());
    Serial.println(readString(interest.getName().get(1).getValue()).c_str());
    Serial.println(interest.getName().size());
    return NDN_ERROR_success;
  }

  std::string method = readString(interest.getName().get(2).getValue());

  if (method == "LED") {
    processLedInterest(readString(interest.getName().get(3).getValue()));
  } else if (method == "SETURL") {
    processSetUrlInterest(readString(interest.getName().get(3).getValue()));
  } else if (method == "GETURL") {
    error = processGetUrlInterest();
  } else if (method == "HELP") {
    error = processHelpInterest();
  } else {
    Serial.print("Unknown method type: ");
    Serial.println(method.c_str());
  }

  return error;
}

static void processLedInterest(const std::string& data) {
  Serial.println("LED");
  if (data.length() != 3) {
    Serial.print("Expect 3 colors but got ");
    Serial.println(data.length());
  } else {
    // get the RGB values
    uint8_t r = data[0];
    uint8_t g = data[1];
    uint8_t b = data[2];

    // set PWM for each led
    analogWrite(r_led, r);
    analogWrite(g_led, g);
    analogWrite(b_led, b);
  }
}

static void processSetUrlInterest(const std::string& data) {
  if (data.length() > 100) {
    Serial.print("Expect url shorter than 100 but got size ");
    Serial.println(data.length());
  } else {
    Serial.print("My Url was: ");
    Serial.println(myUrl.c_str());
    myUrl = data;
    Serial.print("My Url is: ");
    Serial.println(myUrl.c_str());
  }
}

static ndn_Error processGetUrlInterest() {
  return generateResponse(myUrl, "GETURL");
}

static ndn_Error processHelpInterest() {
  return generateResponse("LED, SETURL, GETURL, HELP", "HELP");
}

static ndn_Error generateResponse(const std::string& msg, const std::string& method) {
  // Create the response data packet.
  ndn_NameComponent dataNameComponents[3];
  DataLite data(dataNameComponents, sizeof(dataNameComponents) / sizeof(dataNameComponents[0]), 0, 0);
  data.getName().append("ndnbeacon");
  data.getName().append(RFduinoBLE.deviceName);
  data.getName().append(method.c_str());

  // Set the content to myUrl
  data.setContent(BlobLite((const uint8_t*)msg.c_str(), myUrl.length()));
  
  // Set up the signature with the hmacKeyDigest key locator digest.
  // TODO: Change to ndn_SignatureType_HmacWithSha256Signature when
  //   SignatureHmacWithSha256 is in the NDN-TLV Signature spec:
  //   http://named-data.net/doc/ndn-tlv/signature.html
  data.getSignature().setType(ndn_SignatureType_Sha256WithRsaSignature);
  data.getSignature().getKeyLocator().setType(ndn_KeyLocatorType_KEY_LOCATOR_DIGEST);
  data.getSignature().getKeyLocator().setKeyData(BlobLite(hmacKeyDigest, sizeof(hmacKeyDigest)));
  
  // Encode once to get the signed portion.
  DynamicUInt8ArrayLite output(encoding, sizeof(encoding), 0);
  size_t signedPortionBeginOffset, signedPortionEndOffset;
  ndn_Error error;
  if ((error = Tlv0_2WireFormatLite::encodeData
       (data, &signedPortionBeginOffset, &signedPortionEndOffset, 
  output, &encodingLength))) {
    Serial.print("Encoding failed: ");
    Serial.println(error);
    return error;
  }
  Serial.print("Encoding length is: ");
  Serial.println(encodingLength);
  
  // Get the signature for the signed portion.
  uint8_t signatureValue[ndn_SHA256_DIGEST_SIZE];
  CryptoLite::computeHmacWithSha256
    (hmacKey, sizeof(hmacKey), encoding + signedPortionBeginOffset,
     signedPortionEndOffset - signedPortionBeginOffset, signatureValue);
  data.getSignature().setSignature(BlobLite(signatureValue, ndn_SHA256_DIGEST_SIZE));
  
  // Encode again to include the signature.
  if ((error = Tlv0_2WireFormatLite::encodeData
       (data, &signedPortionBeginOffset, &signedPortionEndOffset, 
  output, &encodingLength))){
    Serial.print("Signature encoding failed: ");
    Serial.println(error);
    return error;
  }
  Serial.print("Encoding length is: ");
  Serial.println(encodingLength);

  send_flag = true;
  return NDN_ERROR_success;
}
