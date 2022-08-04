#pragma once

#ifndef SparkInflux_h
#define SparkInflux_h

#ifndef INFLUXDB_NCOUNT
#define INFLUXDB_NCOUNT 200 // Allow up to these many Key Value pairs. The higher the number the more flash memory you use, so use cautiously.
#endif
#ifndef HTTP_BODYMAX
#define HTTP_BODYMAX 16384
#endif
#ifndef MAX_UPLOADABLE_KEY_VALS_PER_PGN
#define MAX_UPLOADABLE_KEY_VALS_PER_PGN 5 // Allow up to these many Key Value pairs. The higher the number the more flash memory you use, so use cautiously.
#endif
#ifndef MAX_UPLOADABLE_TAG_VALS_PER_PGN
#define MAX_UPLOADABLE_TAG_VALS_PER_PGN 1 // Allow up to these many Tag Value pairs. The higher the number the more flash memory you use, so use cautiously.
#endif

#include <Arduino.h>

#ifndef SparkHTTP_h // We leverage the HEADER struct from SparkHTTP. If we're not using SparkHTTP then we need to redfine it here.
struct HEADER       // A slightly nicer way of handling HTTP headers
{
    String key;
    String value;
};
#endif

struct KEY_VAL
{
    String key;
    float value;
};
struct TAG_VAL
{
    String tag;
    String value;
};
struct POINT
{
    String name;
    TAG_VAL TagValues[MAX_UPLOADABLE_TAG_VALS_PER_PGN];
    KEY_VAL KeyValues[MAX_UPLOADABLE_KEY_VALS_PER_PGN]; // We allow up to 5 key_value pairs in a single POINT
    String timestamp;
};
enum KEYVAL_RESPONSE_ENUM
{
    KEYVAL_SUCCESS,            // We were able to add this key val pair
    KEYVAL_HARD_LIMIT_REACHED, // We have reached the limit of allowable KEY_VAL pairs
    KEYVAL_BODY_LIMIT_REACHED, // We have reached the body limit
};

class spkm_InfluxClient
{
    // A custom InfluxDB Wrapper

public:
    HEADER http_headers[3];
    String http_URL;
    String body = (char *)ps_malloc(HTTP_BODYMAX);

    spkm_InfluxClient(String URL, String TOKEN, String ORG, String BUCKET, String SENSOR_NAME)
    {
        this->URL = URL;
        this->TOKEN = TOKEN;
        this->ORG = ORG;
        this->BUCKET = BUCKET;
        this->SENSOR_NAME = SENSOR_NAME;
        setHttpURL();
        setHttpHeaders();
    }
    void set_SensorName(String name)
    {
        this->SENSOR_NAME = name;
    }
    // Used to add a new point (key value pair)
    KEYVAL_RESPONSE_ENUM addPoint(String name, String key, float value, String timestamp = "")
    {
#ifdef HTTP_BODYMAX
        POINT test_this;
        test_this.name = name;
        test_this.KeyValues[0].key = key;
        test_this.KeyValues[0].value = value;
        test_this.timestamp = timestamp;
        if (this->bodyLimitReached(&test_this) or this->npoint_in_array > INFLUXDB_NCOUNT)
        {
            // Adding this KEY_VAL will tip us over the body limit.
            this->hitOverflow();
            return KEYVAL_BODY_LIMIT_REACHED;
        }
#endif
        // Used to add a new point to the client list.
        if (npoint_in_array >= INFLUXDB_NCOUNT)
        {
            this->hitOverflow();
            return KEYVAL_HARD_LIMIT_REACHED;
        }
        this->POINTS_LIST[npoint_in_array].name = name;
        this->POINTS_LIST[npoint_in_array].KeyValues[0].key = key;
        this->POINTS_LIST[npoint_in_array].KeyValues[0].value = value;
        this->POINTS_LIST[npoint_in_array].timestamp = timestamp;
        this->npoint_in_array += 1;
        this->body_len = http_body().length();
        return KEYVAL_SUCCESS;
    }
    // Used to add a tag value pair to the most recently added point
    KEYVAL_RESPONSE_ENUM addTagValue(String tag, String value)
    {
        return this->addTagValue(tag, value, this->npoint_in_array - 1); // Use the last one
    }
    // Used to add a key value pair to a specic point
    KEYVAL_RESPONSE_ENUM addTagValue(String tag, String value, int index)
    {
#ifdef HTTP_BODYMAX
        if (this->bodyLimitReached())
        {
            // Will adding this KEY_VAL tip us over the body limit?
            this->hitOverflow();
            return KEYVAL_BODY_LIMIT_REACHED;
        }
#endif
        KEYVAL_RESPONSE_ENUM addedSuccessfully = KEYVAL_HARD_LIMIT_REACHED;
        for (int i = 0; i < MAX_UPLOADABLE_TAG_VALS_PER_PGN; i++)
        {
            if (this->POINTS_LIST[index].TagValues[i].tag == nullptr)
            {
                this->POINTS_LIST[index].TagValues[i].tag = tag;
                this->POINTS_LIST[index].TagValues[i].value = value;
                addedSuccessfully = KEYVAL_SUCCESS;
                break;
            }
        }
        this->body_len = http_body().length();
        return addedSuccessfully;
    }
    // Used to add a key value pair to the most recently added point
    KEYVAL_RESPONSE_ENUM addKeyValue(String key, float value)
    {
        return this->addKeyValue(key, value, this->npoint_in_array - 1); // Use the last one
    }
    // Used to add a key value pair to a specic point
    KEYVAL_RESPONSE_ENUM addKeyValue(String key, float value, int index)
    {
#ifdef HTTP_BODYMAX
        if (this->bodyLimitReached())
        {
            // Will adding this KEY_VAL tip us over the body limit?
            this->hitOverflow();
            return KEYVAL_BODY_LIMIT_REACHED;
        }
#endif
        KEYVAL_RESPONSE_ENUM addedSuccessfully = KEYVAL_HARD_LIMIT_REACHED;
        for (int i = 1; i < MAX_UPLOADABLE_KEY_VALS_PER_PGN; i++)
        {
            if (this->POINTS_LIST[index].KeyValues[i].key == nullptr)
            {
                this->POINTS_LIST[index].KeyValues[i].key = key;
                this->POINTS_LIST[index].KeyValues[i].value = value;
                addedSuccessfully = KEYVAL_SUCCESS;
                break;
            }
        }
        this->body_len = http_body().length();
        return addedSuccessfully;
    }

    void clearLastPoint()
    {
        for (int i_kv = 0; i_kv < MAX_UPLOADABLE_KEY_VALS_PER_PGN; i_kv++)
        {
            this->POINTS_LIST[this->npoint_in_array].KeyValues[i_kv] = {};
        }
        for (int i_tv = 0; i_tv < MAX_UPLOADABLE_TAG_VALS_PER_PGN; i_tv++)
        {
            this->POINTS_LIST[this->npoint_in_array].TagValues[i_tv] = {};
        }
        this->npoint_in_array = max(this->npoint_in_array - 1, 0);
        this->body_len = http_body().length();
    }
    void clearPoints()
    {
        for (int n_p = 0; n_p < INFLUXDB_NCOUNT; n_p++)
        {
            this->POINTS_LIST[n_p].name = "";
            this->POINTS_LIST[n_p].timestamp = "";
            for (int i_kv = 0; i_kv < MAX_UPLOADABLE_KEY_VALS_PER_PGN; i_kv++)
            {
                this->POINTS_LIST[n_p].KeyValues[i_kv] = {};
            }
            for (int i_tv = 0; i_tv < MAX_UPLOADABLE_TAG_VALS_PER_PGN; i_tv++)
            {
                this->POINTS_LIST[n_p].TagValues[i_tv] = {};
            }
        }
        this->npoint_in_array = 0;
        this->overflow_hit = false; // reset the overflow, too!
        this->body_len = http_body().length();
    }
    String parsePointAsLineProtocol(POINT point)
    {
        String parsedPoint = "";
        parsedPoint += point.name + ",BOX=";
        parsedPoint += this->SENSOR_NAME;

        for (int i = 0; i < MAX_UPLOADABLE_TAG_VALS_PER_PGN; i++)
        {
            if (point.TagValues[i].tag != nullptr)
            {
                parsedPoint += ",";
                parsedPoint += point.TagValues[i].tag;
                parsedPoint += "=";
                parsedPoint += point.TagValues[i].value;
            }
        }

        for (int i = 0; i < MAX_UPLOADABLE_KEY_VALS_PER_PGN; i++)
        {
            if (point.KeyValues[i].key != nullptr)
            {
                if (i == 0)
                {
                    parsedPoint += " ";
                }
                else
                {
                    parsedPoint += ",";
                }
                parsedPoint += point.KeyValues[i].key;
                parsedPoint += "=";
                parsedPoint += String(point.KeyValues[i].value);
            }
        }
        parsedPoint += " ";
        parsedPoint += String(point.timestamp);
        parsedPoint += '\r';
        parsedPoint += '\n';
        return parsedPoint;
    }

    String http_body(int start = 0, int end = -1)
    {
        body = "";
        if (end == -1 or end > npoint_in_array)
        {
            end = npoint_in_array;
        }
        for (int ind = start; ind < end; ind++)
        {
            body += parsePointAsLineProtocol(this->POINTS_LIST[ind]);
        }
        body = body.substring(0, body.length() - 2); // remove trailing rs and ns
        this->body_len = body.length();
        return body;
    }
    bool overflowed(bool absorb = false)
    {
        // Have we hit the overflow (trying to insert points beyond INFLUXDB_NCOUNT?)
        if (npoint_in_array >= INFLUXDB_NCOUNT)
        {
            this->hitOverflow();
        }
        bool return_overflow = this->overflow_hit;
        if (absorb == true)
        {
            this->overflow_hit = false;
        }
        return return_overflow;
    }

private:
    String URL;
    String TOKEN;
    String ORG;
    String BUCKET;
    String SENSOR_NAME;

    POINT POINTS_LIST[INFLUXDB_NCOUNT];
    int body_len;
    int npoint_in_array = 0;
    bool overflow_hit = false;

    void hitOverflow()
    {
        this->overflow_hit = true;
    }
#ifdef HTTP_BODYMAX
    // Have we reached the body limit size (as set by the SIMCOM chips)?
    bool bodyLimitReached(POINT *test_this = nullptr)
    {
        if (test_this != nullptr)
        {

            return ((this->body_len + parsePointAsLineProtocol(*test_this).length() + 2) > HTTP_BODYMAX);
        }
        return (this->body_len > HTTP_BODYMAX);
    }
#endif

    void setHttpHeaders()
    {
        // Private function to populate the standard headers for use with an HTTP client
        this->http_headers
            [0]
                .key = "Authorization";
        this->http_headers
            [0]
                .value = "Token " + String(INFLUXDB_TOKEN);
        this->http_headers
            [1]
                .key = "Connection";
        this->http_headers
            [1]
                .value = "keep-alive";
    }

    void setHttpURL()
    {
        // Private function to populate the URL for use with an HTTP client
        String loc = "/api/v2/write?org=";
        loc += this->ORG;
        loc += "&bucket=";
        loc += this->BUCKET;
        loc += "&precision=s";
        this->http_URL = loc;
    }
};

#endif

/**
 * @brief The standard InfluxClient is useful for constructing datasets prior to handling/uploading, and then retrieving these datasets later.
 * However, this approach does not work well for Strings (as they can be arbitrarily long), and must be handled in an alternative way anyway.
 * \n
 * The InfluxStringHandler allows us to construct String-friendly Influx lines on a point-by-point bases (i.e. there is no constructable dataset),
 * which means that you MUST HANDLE ANY COLLECTION OF POINTS YOURSELF (e.g. String_POINT your_list[x]).
 * \n
 * @warning It will be up to YOU to ensure your Strings follow valid line-protocol rules, that your HTTP body will fit as expected, you will construct
 * your own headers, and perform your own upload functinoality!
 */
namespace spkm_InfluxStringHandler
{
    struct String_KEY_VAL
    {
        String key;
        String value;
    };
    struct String_POINT
    {
        String name;
        String_KEY_VAL KeyValues[MAX_UPLOADABLE_KEY_VALS_PER_PGN];
        String timestamp;
        String sensor_name = MONITOR_BOX_ID;
    };

    /**
     * @brief Construct a String-friendly Point, only for uploading on a string line-by-line basis
     *
     * @param name The name of your point
     * @param key The first key to use for your point
     * @param value The value for the first key
     * @param timestamp Set a specific timestamp (i.e. when was the point made), otherwise will default to upload time on Influx server side
     * @return String_POINT your returned String_POINT ready for manipulating and/or using
     */
    String_POINT constructStringPoint(String name, String timestamp = "", String sensor_name = "")
    {
        String_POINT point;
        point.name = name;
        point.timestamp = timestamp;
        if (sensor_name.compareTo("") != 0)
        {
            point.sensor_name == sensor_name;
        }
        return point;
    }

    /**
     * @brief Is a given point already full, or can we add more key vals to it?
     *
     * @param Point The point to check
     * @return true The point is full, you cannot add any more key-vals to it!
     * @return false The point is not ful, you may add another key-val to it.
     */
    bool isPointFull(String_POINT Point)
    {
        return (Point.KeyValues[MAX_UPLOADABLE_KEY_VALS_PER_PGN].key != nullptr);
    }

    /**
     * @brief Add key-value data to an existing point (if not already full)
     *
     * @param Point The point to add a Key Value pair to (this is mutated via pointer)
     * @param key The key (String) to add to the point
     * @param value The value (String) to add to the point at the above key
     * @return true If Key values were able to be added to point, false If key values unbable to be added to point (i.e. Point is already full)
     */
    bool addKeyValueToStringPoint(String_POINT *Point, String key, String value)
    {
        for (int i = 0; i < MAX_UPLOADABLE_KEY_VALS_PER_PGN; i++)
        {
            if (Point->KeyValues[i].key == nullptr)
            {
                Point->KeyValues[i].key = key;
                Point->KeyValues[i].value = value;
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Parse a String_POINT as a http-friendly Line Protocol for uploading to InfluxDB (via REST API)
     *
     * @param point // The point you are uploading (we do this on a point-by-point basis)
     * @return String // The returned string, ready for uploading
     */
    String parseStringPointAsLineProtocol(String_POINT point)
    {
        String parsedPoint = "";
        parsedPoint += point.name + ",sensor_id=";
        parsedPoint += point.sensor_name;
        parsedPoint += " ";
        for (int i = 0; i < MAX_UPLOADABLE_KEY_VALS_PER_PGN; i++)
        {
            if (point.KeyValues[i].key != nullptr)
            {
                if (i != 0)
                {
                    parsedPoint += ",";
                }
                parsedPoint += point.KeyValues[i].key;
                parsedPoint += "=0x22";                  // Wrap the value in quotes
                parsedPoint += point.KeyValues[i].value; // Will already be a string
                parsedPoint += "0x22";
            }
        }
        parsedPoint += " ";
        parsedPoint += String(point.timestamp);
        parsedPoint += '\r';
        parsedPoint += '\n';
        return parsedPoint;
    }
}