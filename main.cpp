#include <iostream>
#include <fstream>
#include <vector>
#include <regex>
#include <bits/stdc++.h>

#include <curl/curl.h>

// #define _STORE_MODE
#define BASE_URL        "https://www.plataformaredigir.com.br/"
#define DEFAULT_TIMEOUT 35l
#define DEFAULT_VERBOSE 3

#ifdef _STORE_MODE
#include <mysql-cppconn-8/mysql/jdbc.h>
#include <mysql-cppconn-8/jdbc/mysql_driver.h>
#include <mysql-cppconn-8/jdbc/mysql_connection.h>
#include <mysql-cppconn-8/jdbc/mysql_error.h>

#define MYSQL_HOST  "tcp://127.0.0.1:3306"
#define MYSQL_USER  "root"
#define MYSQL_PASS  ""
#define MYSQL_DB    "redaemon"

#endif

std::string formatAndDecode(std::string str)
{
    std::string decoded(str);
    decoded = std::regex_replace(decoded, std::regex("&([^;]+);"), "?");

    bool cap = true;
    for( unsigned int i=0; i<decoded.length(); i++ )
    {
        if( isalpha(decoded[i]) && cap )
        {
            decoded[i] = toupper(decoded[i]);
            cap = false;
        }
        else if( isspace(decoded[i]) ) cap = true;
        else if( isupper(decoded[i]) && decoded[i-1] != 'X' )
            decoded[i] = tolower(decoded[i]);
    }

    return decoded;
}

class HTTPRequest
{
public:
    HTTPRequest();
    virtual ~HTTPRequest();

    struct
    {
        std::string headers;
        std::string body;
        unsigned long status;

    } response;

    struct
    {
        std::string url;
        std::string method = "GET";
        std::string postData;
        std::string cookies;

    } request;

    virtual void prepare(){}
    virtual bool action(){}
    virtual void check(){}

    bool send();

private:
    CURL *curl = NULL;
    CURLcode res;

    static size_t writeCallback(void *content, size_t size, size_t nmemb, void *userp)
    {
        ((std::string *)userp)->append((const char *)content);
        return size*nmemb;
    }
};

class PRPanel : public HTTPRequest
{
public:
    enum { E_CORRECTED, E_WAITING, E_PROCESSING };

    struct correction_
    {
        double criteriaPoints[5] = {0, 0, 0, 0, 0};
        double totalPoints = 0;

        std::string comment;

    } correction;

    struct essay_
    {
        std::string name;
        std::string description;
        std::string prettyName;
        std::string statusName;
        int status;

        struct correction_ correction;
    };

    struct
    {
        std::string plan;
        std::string expiration;
        unsigned short credits;

    } info;

    std::vector<struct essay_> essay;
    int countCorrected = 0;
    int countWaiting = 0;
    int countProcessing = 0;

    bool action();

    void fetchInfo();
    void getIndex();
    void getEssayById(unsigned short);

    void getEssay(std::string);

    void setCookie(std::string cookie){ authCookie = cookie; }

private:
    std::string authCookie;
};

class PRLog
{
public:
    PRLog();
    ~PRLog();

    enum { LOG_ERROR, LOG_NOTICE, LOG_DEBUG };

    static void msg(std::string msg, unsigned short type)
    {
        if ( type <= DEFAULT_VERBOSE )
        {
            switch( type )
            {
            case LOG_ERROR:
                std::cerr << "[ERROR] " << msg << std::endl;
                break;

            case LOG_NOTICE:
            case LOG_DEBUG:
            default:
                std::cout << "[NOTICE]" << msg << std::endl;
                break;

            }
        }
    }

    bool updateEssayMetadata(struct PRPanel::essay_);

#ifdef _STORE_MODE
private:
    sql::Driver* sqlDriver;
    sql::Connection *sqlConnection;
#endif
};

HTTPRequest::HTTPRequest()
{
    //
}

HTTPRequest::~HTTPRequest()
{
    //
}

bool HTTPRequest::send()
{
    bool retVal = false;

    if ( !curl )
        curl = curl_easy_init();

    if ( curl )
    {
        prepare();
        response = {};

        curl_easy_setopt(curl, CURLOPT_URL, std::string(BASE_URL + request.url).c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, HTTPRequest::writeCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HTTPRequest::writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 2l);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, DEFAULT_TIMEOUT);

        if ( request.url.find("https://") != std::string::npos ) {
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0l);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0l);
        }

        if ( !request.postData.empty() )
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.postData.c_str());

        if ( !request.cookies.empty() )
            curl_easy_setopt(curl, CURLOPT_COOKIE, request.cookies.c_str());

        if ( (res = curl_easy_perform(curl)) == CURLE_OK )
        {
            curl_easy_getinfo(curl, CURLINFO_HTTP_CODE, &response.status);
            retVal = action();
        }
        else
        {
            PRLog::msg("curl_easy_perform(): " + std::string(curl_easy_strerror(res)), PRLog::LOG_ERROR);
            retVal = true;
        }

        curl_easy_cleanup(curl);
        curl = NULL;
    }
    else { PRLog::msg("curl alocation error.", PRLog::LOG_ERROR); retVal = true; }

    return retVal;
}

class PRAuthentication : public HTTPRequest
{
public:
    PRAuthentication()
    {
        request.url = "Account/Login";
    }

    bool login(std::string, std::string);
    bool action();

    std::string cookie(){ return authCookie; }

private:
    std::string authCookie;
};

bool PRAuthentication::login(std::string email, std::string password)
{
    request.postData = "Email=" + email + "&Password=" + password;
    return send();
}

bool PRAuthentication::action()
{
    if( response.headers.find("Location: /Aluno") != std::string::npos )
    {
        std::regex rexp("Set-Cookie: ([^;$]+)");
        std::sregex_iterator next(response.headers.begin(), response.headers.end(), rexp);
        std::sregex_iterator end;

        while ( next != end )
        {
            std::smatch match = *next++;
            authCookie.append(match[1]);
            authCookie.append("; ");
        }

    }
    else
    {
        PRLog::msg("falha na autenticação.", PRLog::LOG_ERROR);
        return true;
    }
}

bool PRPanel::action()
{
    if ( response.status != 200 )
    {
        PRLog::msg("expired cookies or website error.", PRLog::LOG_ERROR);
        return true;
    }

    return false;
}

void PRPanel::fetchInfo()
{
    request.cookies = authCookie;
    request.url = "Account/Creditos";

    if( !send() )
    {
        std::regex rexp("<td([^>]*)>([^<]+)");
        std::sregex_iterator next(response.body.begin(), response.body.end(), rexp);
        std::sregex_iterator end;

        std::vector<std::string> values;

        while ( next != end )
        {
            std::smatch match = *next++;
            values.push_back(match[2]);
        }

        if ( !values.size() )
        {
            info.plan = "Expired";
            info.expiration = "-";
            info.credits = 0;

            return;
        }

        values.at(0) = formatAndDecode(values.at(0));

        info.plan = std::move(values.at(0));
        info.expiration = std::move(values.at(1));
        info.credits = atoi(std::move(values.at(2).c_str()));


    }
}

void PRPanel::getIndex()
{
    request.cookies = authCookie;
    request.url = "Aluno/PlanoIndividual";

    if( !send() )
    {
        std::string buffer(response.body);

        std::regex rexp("<h2 class=\"f-size14\" style=\"font-weight:bold;([^>]*)>([^<$]+)");
        std::sregex_iterator next(response.body.begin(), response.body.end(), rexp);
        std::sregex_iterator end;

        while ( next != end )
        {
            std::smatch match = *next++;
            essay.push_back(essay_{});

            std::smatch mDesc, mPrettyName, mStatus;
            std::regex_search(buffer, mPrettyName, std::regex("<a class=\"learn-more\" href=\"([^\"]+)"));
            std::regex_search(buffer, mDesc, std::regex("<div class=\"lineheight16\">([^<]+)"));

            std::string sPiece = buffer.substr(mPrettyName.position(), 440);

            std::regex statusRegex("<span class='label label-(success|warning)'>([^<]+)");
            std::sregex_iterator statusNext(sPiece.begin(), sPiece.end(), statusRegex), statusEnd;
            unsigned short count = 0;

            while ( statusNext != statusEnd )
            {
                std::smatch sMatch = *statusNext++;
                if ( sMatch[2] == "Corrigida" ){ essay.back().statusName= sMatch[2]; essay.back().status = 0; countCorrected++; }
                if ( sMatch[2] == "Aguardando correção" ){ essay.back().statusName= sMatch[2];essay.back().status= 1; countWaiting++; }

                count++;
            }

            if ( !count && !essay.back().status ){ essay.back().statusName= "Em correção"; essay.back().status = 2; countProcessing++; }

            essay.back().name = match[2];
            essay.back().description = mDesc[1];
            essay.back().prettyName = mPrettyName[1].str().substr(mPrettyName[1].str().find_last_of("/"));

            buffer = buffer.substr(mPrettyName.position() + mPrettyName.length());
        }
    }
}

void PRPanel::getEssay(std::string link)
{
    request.cookies = authCookie;
    request.url = "Aluno/DetalheTema" + link;

    if( !send() )
    {
        std::string buffer(response.body);

        std::regex reNota("<strong>([^/]+)");
        std::smatch mNota;

        std::regex_search(response.body, mNota, reNota);
        correction.totalPoints = atof(mNota[1].str().c_str());
    }
}

PRLog::PRLog()
{
#ifdef _STORE_MODE
    try
    {
        sqlDriver = sql::mysql::get_driver_instance();
        sqlConnection = sqlDriver->connect(MYSQL_HOST, MYSQL_USER, MYSQL_PASS);
        sqlConnection->setSchema(MYSQL_DB);
    }
    catch( sql::SQLException &e )
    {
        msg(e.what(), LOG_ERROR);
    }
#endif
}

PRLog::~PRLog()
{
#ifdef _STORE_MODE
    delete sqlConnection;

#endif
}

bool PRLog::updateEssayMetadata(struct PRPanel::essay_ essay)
{
#ifdef _STORE_MODE
    sql::PreparedStatement *stmt = sqlConnection->prepareStatement("INSERT INTO essays ("
                                                               "essay_name, essay_prettyname, essay_description,"
                                                               "essay_status, essay_postdt, essay_correctiondt) "
                                                               "VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP(), NULL) ");

    stmt->setString(1, essay.name);
    stmt->setString(2, essay.prettyName);
    stmt->setString(3, essay.description);
    stmt->setInt(4, essay.status);

    stmt->execute();

    delete stmt;

#else
    return false;
#endif
}

int main()
{
    PRLog* log = new PRLog;

    PRAuthentication* auth = new PRAuthentication;
    std::cout << "Efetuando login..." << std::endl << std::endl;

    if ( !auth->login("user@email.com", "password123") )
    {
        PRPanel *panel = new PRPanel;
        panel->setCookie(auth->cookie());
        panel->fetchInfo();

        std::cout << "Plano: " << panel->info.plan << std::endl;
        std::cout << "Créditos: " << panel->info.credits << std::endl;
        std::cout << "Validade: " << panel->info.expiration << std::endl << std::endl;

        panel->getIndex();
        std::vector<std::string> corrected, waiting, processing;

        unsigned short num = 0;
        for ( auto redacao : panel->essay )
        {
            log->updateEssayMetadata(redacao);

            std::cout << "Red. #" << num++ << std::endl;
            std::cout << "Nome: " << formatAndDecode(redacao.name) << std::endl;
            std::cout << "Desc: " << formatAndDecode(redacao.description) << std::endl;
            std::cout << "Link: " << redacao.prettyName << std::endl;
            std::cout << "Status: " << redacao.statusName << std::endl << std::endl;

            switch( redacao.status )
            {
            case PRPanel::E_CORRECTED:  corrected.push_back(redacao.prettyName);    break;
            case PRPanel::E_WAITING:    waiting.push_back(redacao.prettyName);      break;
            case PRPanel::E_PROCESSING: processing.push_back(redacao.prettyName);   break;
            }
        }

        std::cout << "Total: " << num << std::endl;
        std::cout << "Corrigidas: " << panel->countCorrected << std::endl;
        std::cout << "Aguardando: " << panel->countWaiting << std::endl;
        std::cout << "Em correção: " << panel->countProcessing << std::endl;

        std::ofstream logStream(std::string(::getenv("HOME")) + "/.redigirrc");
        logStream << num << "|" << panel->countCorrected << "|" <<
                     panel->countWaiting << "|" << panel->countProcessing << std::endl;

//        panel->getEssay(panel->essay.at(3).prettyName);
//        std::cout << "Nota da redação 3: " << panel->correction.totalPoints << "/1000" << std::endl;
    }

    return 0;
}
