#include "SC_RemoteAppTool.h"
#include "Utils/ProgramOutputDialog.h"
#include "SimCenterAppWidget.h"
#include <RemoteService.h>
#include <SimCenterPreferences.h>
#include <ZipUtils/ZipUtils.h>
#include <RemoteJobManager.h>
#include <SC_ResultsWidget.h>
#include <Utils/RelativePathResolver.h>

#include <QDebug>
#include <QStackedWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QDir>
#include <QApplication>
#include <QCoreApplication>
#include <QLineEdit>
#include <QJsonDocument>
#include <QMessageBox>
#include <QFileDialog>


SC_RemoteAppTool::SC_RemoteAppTool(QString appName,
				   QList<QString> theQueus,
				   RemoteService *theRemoteService,
				   SimCenterAppWidget* theEnclosedApp,
				   QDialog *enclosingDialog)
:SimCenterAppWidget(), theApp(theEnclosedApp), theService(theRemoteService), tapisAppName(appName), queus(theQueus)
{
  QVBoxLayout *theMainLayout = new QVBoxLayout(this);
  theMainLayout->addWidget(theApp);
  

  QGridLayout *theButtonLayout = new QGridLayout();
  QPushButton *fileLoadButton = new QPushButton("LOAD File");
  QPushButton *fileSaveButton = new QPushButton("SAVE File");  
  QPushButton *runRemoteButton = new QPushButton("RUN at DesignSafe");
  QPushButton *getRemoteButton = new QPushButton("GET from DesignSafe");

  theButtonLayout->addWidget(fileLoadButton,0,0);
  theButtonLayout->addWidget(fileSaveButton,0,1);  
  theButtonLayout->addWidget(runRemoteButton,0,2);
  theButtonLayout->addWidget(getRemoteButton,0,3);
  
  theMainLayout->addLayout(theButtonLayout);

  // now create the Dialog for the Remote Applications

  QDialog *theRemoteDialog = new QDialog(this);
  // QString workflowScriptName = "FMK-NAME";
  QString shortDirName = QCoreApplication::applicationName() + ":";

  //shortDirName = workflowScriptName;
  //shortDirName = name.chopped(3); // remove .py
  //shortDirName.chop(3);
  
  QGridLayout *remoteLayout = new QGridLayout();
  QLabel *nameLabel = new QLabel();
  
  int numRow = 0;

  remoteLayout->addWidget(new QLabel("Job Name:"), numRow,0);
  nameLineEdit = new QLineEdit();
  nameLineEdit->setToolTip(tr("A meaningful name to provide for you to remember run later (days and weeks from now)"));
  remoteLayout->addWidget(nameLineEdit,numRow,1);

  int numNode = 1;
  int maxProcPerNode = 56; //theApp->getMaxNumProcessors(56);
  
  numRow++;
  numCPU_LineEdit = new QLineEdit();
  remoteLayout->addWidget(new QLabel("Num Nodes:"),numRow,0);  
  remoteLayout->addWidget(numCPU_LineEdit,numRow,1);
  numCPU_LineEdit->setText("1");
    
  numRow++;

  if (queus.first() == "gpu-a100") {
    
    // lonestar 6 .. only set up for gpu-a100
    maxProcPerNode = 128; //theApp->getMaxNumProcessors(56);

    QLabel *numGPU_Label = new QLabel();
    remoteLayout->addWidget(new QLabel("Num GPUs:"),numRow,0);
    
    numGPU_LineEdit = new QLineEdit();
    numGPU_LineEdit->setText("3");
    numGPU_LineEdit->setToolTip(tr("Total # of GPUs to use (across all nodes)"));
    remoteLayout->addWidget(numGPU_LineEdit,numRow,1);
    
    numRow++;
    
  } else {
    numGPU_LineEdit = NULL;
  }


  remoteLayout->addWidget(new QLabel("Num Processors Per Node:"),numRow,0);    
  numProcessorsLineEdit = new QLineEdit();
  numProcessorsLineEdit->setText(QString::number(maxProcPerNode));
  numProcessorsLineEdit->setToolTip(tr("Total # of Processes to Start"));

  remoteLayout->addWidget(numProcessorsLineEdit,numRow,1);

  numRow++;
  
  remoteLayout->addWidget(new QLabel("Max Run Time:"),numRow,0);
  runtimeLineEdit = new QLineEdit();
  if (queus.first() == "gpu-a100") 
    runtimeLineEdit->setText("02:00:00");
  else
    runtimeLineEdit->setText("00:20:00");
  
  runtimeLineEdit->setToolTip(tr("Run time Limit on running Job hours:Min:Sec. Job will be stopped if while running it exceeds this"));
  remoteLayout->addWidget(runtimeLineEdit,numRow,1);
  
  numRow++;
  submitButton = new QPushButton();
  submitButton->setText("Submit");
  submitButton->setToolTip(tr("Press to launch job on remote machine. After pressing, window closes when Job Starts"));
  remoteLayout->addWidget(submitButton,numRow,1);
  
  theRemoteDialog->setLayout(remoteLayout);
  theRemoteDialog->hide();

  
  connect(fileLoadButton, &QPushButton::clicked, this,
	  [=]() {
	    //QString fileName=QFileDialog::getOpenFileName(this,tr("Open File"),"C://", "All files (*.*)");
      QString fileName=QFileDialog::getOpenFileName(this,tr("Open JSON File"),"", "JSON file (*.json)"); // sy - to continue from the previously visited directory
	    QFile file(fileName);
	    if (!file.open(QFile::ReadOnly | QFile::Text)) {
	      emit errorMessage(QString("Could Not Open File: ") + fileName);
	      return -1;
	    }

	    //
	    // place contents of file into json object
	    //
	    
	    QString val;
	    val=file.readAll();
	    QJsonDocument doc = QJsonDocument::fromJson(val.toUtf8());
	    QJsonObject jsonObj = doc.object();

	    // close file
	    file.close();
	    
	    //Resolve absolute paths from relative ones
	    QFileInfo fileInfo(fileName);
	    SCUtils::ResolveAbsolutePaths(jsonObj, fileInfo.dir());    
    
	    //
	    // clear current and input from new JSON
	    //
	    
	    theApp->clear();	    
	    theApp->inputFromJSON(jsonObj);
	  });


  connect(fileSaveButton, &QPushButton::clicked, this,
	  [=]() {

      QString fileName=QFileDialog::getSaveFileName(this, tr("Save JSON File"),"", "JSON file (*.json)"); // sy - to continue from the previously visited directory
	    QFile file(fileName);

	    if (!file.open(QFile::WriteOnly | QFile::Text)) {
	      QMessageBox::warning(this, tr("Application"),
				   tr("Cannot write file %1:\n%2.")
				   .arg(QDir::toNativeSeparators(fileName),
					file.errorString()));
	      return false;
	    }
	    
	    //
	    // create a json object, fill it in & then use a QJsonDocument
	    // to write the contents of the object to the file in JSON format
	    //
	    
	    QJsonObject json;
	    theApp->outputToJSON(json);
	    
	    //Resolve relative paths before saving
	    QFileInfo fileInfo(fileName);
	    SCUtils::ResolveRelativePaths(json, fileInfo.dir());
	    
	    QJsonDocument doc(json);
	    file.write(doc.toJson());
	    
	    // close file
	    file.close();	    
	    
	  });
  
  connect(getRemoteButton,&QPushButton::clicked, this, [=](){
    this->onGetRemoteButtonPressed();
  });
  
  connect(runRemoteButton,&QPushButton::clicked, this, [=](){
    theRemoteDialog->show();
  });    

  connect(submitButton,&QPushButton::clicked,this, [=]() {
    this->submitButtonPressed();
    theRemoteDialog->close();
  });  

  if (enclosingDialog != nullptr) {
    QPushButton *closeButton = new QPushButton("Close");  
    theButtonLayout->addWidget(closeButton,0,4);
    connect(closeButton,&QPushButton::clicked,enclosingDialog,&QDialog::close);
    connect(closeButton,&QPushButton::clicked,theRemoteDialog,&QDialog::close);
    connect(closeButton,&QPushButton::clicked,[=]() {
      theJobManager->hide();
    });
  }


  
  QStringList filesToDownload; filesToDownload << "scInput.json" << "results.zip" << "inputData.zip";
  theJobManager = new RemoteJobManager(theService);
  theJobManager->setFilesToDownload(filesToDownload);
  theJobManager->hide();
  connect(theJobManager,SIGNAL(processResults(QString&)), this, SLOT(processResults(QString&)));
}

SC_RemoteAppTool::~SC_RemoteAppTool()
{
  qDebug() << "SC_RemoteAppTool::Destructor";
}

void SC_RemoteAppTool::clear()
{
  theApp->clear();
}

// this method display the RemoteApp dialog widget

void
SC_RemoteAppTool::submitButtonPressed() {

  //
  // first check if logged in
  //
  
  bool loggedIn = theService->isLoggedIn();

  if (loggedIn != true) {
    errorMessage("ERROR - You Need to Login to DesignSafe to run a remote application.");
    QMessageBox msg;
    msg.setText("You need to go back to the Main Application and Login to DesignSafe to run a remote application.");
    msg.show();
    return;
  }

  //
  // create tmp directory in which we will place files to be sent
  // 
  

  QDir workDir(SimCenterPreferences::getInstance()->getRemoteWorkDir());

  QUuid uniqueName = QUuid::createUuid();
  QString strUnique = uniqueName.toString();
  strUnique = strUnique.mid(1,36);
  tmpDirName = QString("tmp.SimCenter") + strUnique;
  
  //QString tmpDirName = QString("tmp.SimCenter");  
  QString tmpDirectory = workDir.absoluteFilePath(tmpDirName);
  QDir destinationDirectory(tmpDirectory);

  if(destinationDirectory.exists()) {
    destinationDirectory.removeRecursively();
  } else
    destinationDirectory.mkpath(tmpDirectory);

  //
  // in tmpDir, create another "inputs" in which we will place all the files needed by the app & then zip it up
  //
  
  QString inputsDirectory  = destinationDirectory.absoluteFilePath("inputData");
  destinationDirectory.mkpath(inputsDirectory);

  QString zipFile(destinationDirectory.absoluteFilePath("inputData.zip"));
  QDir inputDataDir(destinationDirectory.absoluteFilePath("inputData"));  
  
  theApp->copyFiles(inputsDirectory);  
  
  ZipUtils::ZipFolder(inputDataDir, zipFile);

  // remove inputData so not sent
  inputDataDir.removeRecursively();
  
  //
  // in tmpDir create the input file
  //

  QString inputFile = destinationDirectory.absoluteFilePath("scInput.json");
  
  QFile file(inputFile);
  if (!file.open(QFile::WriteOnly | QFile::Text)) {
    //errorMessage();
    return;
  }
  
  QJsonObject json;

  QJsonObject appData;
  theApp->outputAppDataToJSON(json);
  json["ApplicationData"]=appData;
  
  theApp->outputToJSON(json);

  json["workingDir"]=SimCenterPreferences::getInstance()->getRemoteWorkDir(); 
  json["runDir"]=tmpDirectory;
  json["remoteAppDir"]=SimCenterPreferences::getInstance()->getRemoteAppDir();    
  json["runType"]=QString("runningRemote");
  int nodeCount = numCPU_LineEdit->text().toInt();
  int numProcessorsPerNode = numProcessorsLineEdit->text().toInt();
  json["nodeCount"]=nodeCount;
  json["numP"]=nodeCount*numProcessorsPerNode;    
  json["processorsOnEachNode"]=numProcessorsPerNode;
  if (numGPU_LineEdit != NULL) {
    int gpuCount = numGPU_LineEdit->text().toInt();
    json["gpus"]=gpuCount;
  }

  QJsonDocument doc(json);
  file.write(doc.toJson());
  file.close();

  //
  // now send directory containing inputFile and inputData.zip across
  //

  QString dirName = destinationDirectory.dirName();
  
  QString remoteHomeDirPath = theService->getHomeDir();
  if (remoteHomeDirPath.isEmpty()) {
    qDebug() << "RemoteApplication:: - remoteHomeDir is empty!!";
    return;
  }
  remoteDirectory = remoteHomeDirPath + QString("/") + dirName;
  submitButton->setEnabled(false);
  
  connect(theService, SIGNAL(uploadDirectoryReturn(bool)), this, SLOT(uploadDirReturn(bool)));
  theService->uploadDirectoryCall(tmpDirectory, remoteHomeDirPath);        

}  


void
SC_RemoteAppTool::uploadDirReturn(bool result)
{
  disconnect(theService, SIGNAL(uploadDirectoryReturn(bool)), this, SLOT(uploadDirReturn(bool)));
    
  //
  // now start the app
  //

  if (result == true) {
    
    //
    // create the json needed to run the remote application
    //
    
    QJsonObject job;
    
    //submitButton->setDisabled(true);
    int nodeCount = numCPU_LineEdit->text().toInt();
    int numProcessorsPerNode = numProcessorsLineEdit->text().toInt();
    
    QString queue; // queuu to send job to
    QString firstQueue = queus.first();
    if (firstQueue == "gpu-a100") {

      queue = "gpu-a100";

    } else { // Frontera

      queue = "small";
      //QString queue = "development";
      if (nodeCount > 2)
        queue = "normal";
      if (nodeCount > 512)
        queue = "large";
    }
    
    QString shortDirName = QCoreApplication::applicationName() + ": ";
    
    job["name"]=shortDirName + nameLineEdit->text();
    job["nodeCount"]=nodeCount;
    //job["processorsPerNode"]=nodeCount*numProcessorsPerNode; // DesignSafe has inconsistant documentation
    job["processorsOnEachNode"]=numProcessorsPerNode;
    job["maxRunTime"]=runtimeLineEdit->text();

    //
    // hard code the queue stuff for this release, wil; need more info on cores, min counts
    //

    
    job["appId"]=tapisAppName;
    job["memoryPerNode"]= "1GB";
    job["archive"]=true;
    job["batchQueue"]=queue;      
    job["archivePath"]="";
    job["archiveSystem"]="designsafe.storage.default";

    QJsonObject parameters;    
    parameters["inputFile"]="scInput.json";
    parameters["modules"]="petsc,python3";
    theApp->outputAppDataToJSON(parameters); //< sets maxRunTime in parameters
    
    // Check if maxRunTime is set in job["parameters"] already and if it is not null
    // Trying not to overwrite any correctly set maxRunTime param
    QString parameterName = "maxRunTime";
    if (parameters.contains(parameterName)) {
      QString paramTimeQString = parameters[parameterName].toString();
      QString jobTimeQString = job[parameterName].toString();
      auto secToHHMMSS = [](int totalNumberOfSeconds) {
          if (totalNumberOfSeconds < 0) {
              totalNumberOfSeconds = 0;
          } else if (totalNumberOfSeconds > 99 * 60 * 60 + 59 * 60 + 59) {
              totalNumberOfSeconds = 99 * 60 * 60 + 59 * 60 + 59;
          }
          int seconds = totalNumberOfSeconds % 60;
          int minutes = (totalNumberOfSeconds / 60) % 60;
          int hours = (totalNumberOfSeconds / 60 / 60);
          return QString("%1:%2:%3")
              .arg(hours, 2, 10, QChar('0'))
              .arg(minutes, 2, 10, QChar('0'))
              .arg(seconds, 2, 10, QChar('0'));
      };
      
      QTime jobTime = QTime::fromString(jobTimeQString, "hh:mm:ss");
      QTime paramTime = QTime::fromString(paramTimeQString, "hh:mm:ss");

      int job_sec = jobTime.hour() * 60 * 60 + jobTime.minute() * 60 + jobTime.second();
      int param_sec = paramTime.hour() * 60 * 60 + paramTime.minute() * 60 + paramTime.second();


      if (job_sec < 300) {
        job_sec = 300; // Don't fall below minimum time, needed for file transfers / zipping
        jobTimeQString = secToHHMMSS(job_sec);
        job[parameterName] = jobTimeQString;
        qDebug() << "RemoteApplication::uploadDirReturn - WARN: maxRunTime in tapis job['maxRunTime'] requested by user is below minimum request, setting to 00:05:00 hh:mm:ss...";
      }

      if (param_sec > job_sec) {
        param_sec = job_sec; // Don't exceed total job wall time
        // paramTimeQString = QTime::fromString(param_sec, "hh:mm:ss").toString();
        paramTimeQString = secToHHMMSS(param_sec);

        qDebug() << "RemoteApplication::uploadDirReturn - WARN: maxRunTime in tapis job['parameters']['maxRunTime'] is above job['maxRunTime'], setting to job['maxRunTime']...";
      }

      parameters[parameterName] = paramTimeQString;
    
  //     if (job.contains("parameters")) {
  //       if (job["parameters"].isObject()) {
  //         if (!job["parameters"].toObject().contains(parameterName) || (job["parameters"].toObject().contains(parameterName) && job["parameters"].toObject()[parameterName].isNull())) { // TODO: Check if this carries over per job....
  //           QJsonObject temp_parameters = job["parameters"].toObject();
  //           QString jobParamTimeQString = temp_parameters[parameterName].toString();
  //           temp_parameters[parameterName] = parameters[parameterName].toString();

  //           QTime jobTime = QTime::fromString(jobTimeQString, "hh:mm:ss");
  //           QTime paramTime = QTime::fromString(paramTimeQString, "hh:mm:ss");
  //           int job_sec = jobTime.hour() * 60 * 60 + jobTime.minute() * 60 + jobTime.second();
  //           int param_sec = paramTime.hour() * 60 * 60 + paramTime.minute() * 60 + paramTime.second();

  //           // qDebug () << "RemoteApplication::uploadDirReturn - INFO: job[parameterName]: " << job[parameterName] << ", extraParameters[parameterName]: " << extraParameters[parameterName];
  //           qDebug () << "RemoteApplication::uploadDirReturn - INFO: jobTimeQString: " << jobTimeQString << ", paramTimeQString: " << paramTimeQString;
  //           qDebug () << "RemoteApplication::uploadDirReturn - INFO: jobTime: " << jobTime << ", paramTime: " << paramTime;
  //           qDebug () << "RemoteApplication::uploadDirReturn - INFO: job_sec: " << job_sec << ", param_sec: " << param_sec;

  //           // Assume we need ATLEAST 3 minutes for main wrapper script. Add ~2 minutes for zipping results
  //           int MIN_RUN_TIME_HH = 0;
  //           int MIN_RUN_TIME_MM = 5;
  //           int MIN_RUN_TIME_SS = 0;
  //           int MIN_RUN_TIME_SEC = (MIN_RUN_TIME_HH * 60 * 60) + (MIN_RUN_TIME_MM * 60) + MIN_RUN_TIME_SS;
  //           // Works upto 99 hours, 59 minutes, 59 seconds
  //           // https://stackoverflow.com/questions/16419333/qt-c-convert-seconds-to-formatted-string-hhmmss

  //           QString MIN_RUN_TIME_QSTRING = secToHHMMSS(MIN_RUN_TIME_SEC); // Minimum job time, e.g. 5min, "hh:mm:ss"
  //           qDebug() << "RemoteApplication::uploadDirReturn - INFO: MIN_RUN_TIME_SEC: " << MIN_RUN_TIME_SEC << ", MIN_RUN_TIME_QSTRING: " << MIN_RUN_TIME_QSTRING;

  //           // Don't fall below minimum time, needed for file transfers / zipping
  //           if (job_sec < MIN_RUN_TIME_SEC) {
  //               job_sec = MIN_RUN_TIME_SEC;
  //               jobTimeQString = secToHHMMSS(job_sec);
  //               job[parameterName] = jobTimeQString;
  //               qDebug() << "RemoteApplication::uploadDirReturn - WARN: maxRunTime in tapis job['maxRunTime'] requested by user is below minimum request, setting to 00:05:00 hh:mm:ss...";
  //           }
  //           if (param_sec < MIN_RUN_TIME_SEC) {
  //               param_sec = MIN_RUN_TIME_SEC;
  //               paramTimeQString = secToHHMMSS(param_sec);
  //               // extraParameters[parameterName] = secToHHMMSS(param_sec); 
  //               qDebug() << "RemoteApplication::uploadDirReturn - WARN: maxRunTime in tapis job['parameters']['maxRunTime'] is below minimum request, setting to 00:05:00 hh:mm:ss...";
  //           }
  //           // Don't exceed total job wall time, or commands may hang and job --> Fail
  //           qDebug() << "RemoteApplication::uploadDirReturn - INFO: job_sec: " << job_sec << ", param_sec: " << param_sec;
  //           if (param_sec > job_sec) {
  //               param_sec = job_sec; // Don't exceed total job wall time
  //               paramTimeQString = secToHHMMSS(param_sec);
  //           }
  //           qDebug() << "RemoteApplication::uploadDirReturn - INFO: job_sec: " << job_sec << ", param_sec: " << param_sec;

  //           // Update the tapis app extra parameters' maxRunTime. Available in the job's wrapper.sh as ${maxRunTime}, "hh:mm:ss"
  //           temp_parameters[parameterName] = paramTimeQString;
  //           qDebug () << "RemoteApplication::uploadDirReturn - INFO: paramTimeQString: " << paramTimeQString << ", parameters[parameterName]: " << parameters[parameterName];
  //           // }

  // //
  //           qDebug() << "SC_RemoteAppTool::uploadDirReturn() - Overwriting maxRunTime in job[\"parameters\"]";
            
  //           parameters[parameterName] = paramTimeQString;
  //           job[parameterName] = jobTimeQString;
  //           // job["parameters"] = temp_parameters;
  //         }
  //       }
  //     }
    }
    job["parameters"]=parameters;

    QJsonObject inputs;
    inputs["inputDirectory"]=remoteDirectory;
    job["inputs"]=inputs;

    qDebug() << job;

    connect(theService,SIGNAL(startJobReturn(QString)), this, SLOT(startJobReturn(QString)));      
    theService->startJobCall(job);
    
  } else {
    submitButton->setEnabled(true);          
  }
}

void
SC_RemoteAppTool::startJobReturn(QString result)
{
  //
  // job started
  //

  disconnect(theService,SIGNAL(startJobReturn(QString)), nullptr, nullptr);      
  Q_UNUSED(result);
  submitButton->setEnabled(true);      
}


void
SC_RemoteAppTool::onGetRemoteButtonPressed() {

    this->errorMessage("");

    bool loggedIn = theService->isLoggedIn();

    if (loggedIn == true) {

        theJobManager->hide();
        theJobManager->updateJobTable("");
        theJobManager->show();

    } else {
        errorMessage("ERROR - You Need to Login");
    }  
}

void
SC_RemoteAppTool::processResults(QString &dirName){

  //
  // get results widget from app and process
  //

  QString localDir = SimCenterPreferences::getInstance()->getRemoteWorkDir();
  QDir localWork(localDir);
  
  if (!localWork.exists())
    if (!localWork.mkpath(localDir)) {
      emit errorMessage(QString("Could not create Working Dir: ") + localDir + QString(" . Try using an existing directory or make sure you have permission to create the working directory."));
      return;
    }
  
    
  SC_ResultsWidget *theResults=theApp->getResultsWidget();
  if (theResults == NULL) {
    this->errorMessage("FATAL - SC_RemoteAppTool received NULL pointer theResults from theApp->getResultsWidget()... skipping theResults->processResults()");
    return;
  }

  QString blankFileName("scInput.json");
  theResults->processResults(blankFileName,dirName);

}



	     





